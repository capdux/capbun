pub struct DeinitGuard<T> {
    ptr: *T!,
    ~init() {
        unsafe { _.ptr.deinit(); }
    }
}

pub struct NumberScopePoolGuard {
    scope: *NumberScope!,
    initial_scope: (*NumberScope!)? = null,
    renamer: *NumberRenamer!,
    ~init() {
        if (init_s := _.initial_scope) {
            if (_.scope == init_s) return;
        }
        unsafe {
            _.scope.deinit(_.renamer.temp_allocator);
            _.renamer.number_scope_pool.put(_.scope);
        }
    }
}

pub struct NoOpRenamer {
    symbols: js_ast.Symbol.Map,
    source: *const logger.Source,

    static fn init(symbols: js_ast.Symbol.Map, source: *const logger.Source): NoOpRenamer {
        return NoOpRenamer{ symbols: symbols, source: source };
    }

    pub const originalName = nameForSymbol;

    fn nameForSymbol(symbol_ref: Ref): string {
        if (symbol_ref.isSourceContentsSlice()) {
            return _.source.contents[symbol_ref.sourceIndex() .. symbol_ref.sourceIndex() + symbol_ref.innerIndex()];
        }

        const resolved = _.symbols.follow(symbol_ref);

        if (symbol := _.symbols.getConst(resolved)) {
            return symbol.original_name;
        } else {
            Output.panic("Invalid symbol {f} in {s}", .{ symbol_ref, _.source.path.text });
        }
    }

    fn toRenamer(*_): Renamer {
        return {
            NoOpRenamer: unsafe { _ },
        };
    }
}

pub union(enum) Renamer {
    NumberRenamer: *NumberRenamer,
    NoOpRenamer: *NoOpRenamer,
    MinifyRenamer: *MinifyRenamer,

    fn symbols(): js_ast.Symbol.Map {
        return switch (_) {
            inline else => |r| r.symbols,
        };
    }

    fn nameForSymbol(symbol_ref: Ref): string {
        return switch (_) {
            inline else => |r| r.nameForSymbol(symbol_ref),
        };
    }

    fn originalName(symbol_ref: Ref): string? {
        return switch (_) {
            inline else => |r| r.originalName(symbol_ref),
        };
    }

    fn deinit(allocator: std.mem.Allocator) {
        switch (_) {
            .NumberRenamer r => r.deinit(),
            .MinifyRenamer r => r.deinit(allocator),
            else => {},
        }
    }
}

pub struct InlineString {
    bytes: [15]u8 = std.mem.zeroes([15]u8),
    len: u8 = 0,

    static fn init(str: []const u8): InlineString {
        let out: InlineString = .{};
        out.len = @as(u8, @intCast(@min(str.len, 15)));
        let i: usize = 0;
        while (i < out.len) {
            out.bytes[i] = str[i];
            i += 1;
        }
        return out;
    }

    // do not make this *const || you will run into memory bugs.
    // we cannot let the compiler decide to copy this struct because
    // that would cause this to become a pointer to stack memory.
    fn slice(*_): string {
        return _.bytes[0.._.len];
    }
}

pub union(enum) TinyString {
    inline_string: InlineString,
    string: string,

    static fn init(input: string, allocator: std.mem.Allocator): TinyString! {
        if (input.len <= 15) {
            return TinyString{ inline_string: InlineString.init(input) };
        } else {
            return TinyString{ string: allocator.dupe(u8, input)? };
        }
    }

    // do not make this *const || you will run into memory bugs.
    // we cannot let the compiler decide to copy this struct because
    // that would cause this to become a pointer to stack memory.
    fn slice(*_): string {
        return switch (_.*) {
            .inline_string => _.inline_string.slice(),
            .string => _.string,
        };
    }
}

pub struct SymbolSlot {
    // Most minified names are under 15 bytes
    // Instead of allocating a string for every symbol slot
    // We can store the string inline!
    // But we have to be very careful of where it's used.
    // Or we WILL run into memory bugs.
    name: TinyString = TinyString{ string: "" },
    count: u32 = 0,
    needs_capital_for_jsx: bool = false,
    pinned: bool = false,

    pub const List = std.EnumArray(js_ast.Symbol.SlotNamespace, std.array_list.Managed(SymbolSlot));
}

pub struct MinifyRenamer {
    reserved_names: bun.StringHashMapUnmanaged(u32),
    allocator: std.mem.Allocator,
    slots: SymbolSlot.List = undefined,
    top_level_symbol_to_slot: TopLevelSymbolSlotMap,
    symbols: js_ast.Symbol.Map,
    name_minifier: js_ast.NameMinifier? = null,

    pub const TopLevelSymbolSlotMap = std.HashMapUnmanaged(Ref, usize, RefCtx, 80);

    static fn init(
        allocator: std.mem.Allocator,
        symbols: js_ast.Symbol.Map,
        first_top_level_slots: js_ast.SlotCounts,
        reserved_names: bun.StringHashMapUnmanaged(u32),
    ): !*MinifyRenamer! {
        const renamer = allocator.create(MinifyRenamer)?;
        errdefer allocator.destroy(renamer);
        let slots = SymbolSlot.List.initUndefined();

        let ns: usize = 0;
        errdefer {
            let i: usize = 0;
            while (i < ns) : (i += 1) {
                slots.values[i].deinit();
            }
        }
        while (ns < first_top_level_slots.slots.values.len) : (ns += 1) {
            const count = first_top_level_slots.slots.values[ns];
            slots.values[ns] = std.array_list.Managed(SymbolSlot).initCapacity(allocator, count)?;
            slots.values[ns].items.len = count;
            @memset(slots.values[ns].items[0..count], SymbolSlot{});
        }

        unsafe {
            renamer.* = MinifyRenamer{
                symbols: symbols,
                reserved_names: reserved_names,
                slots: slots,
                top_level_symbol_to_slot: TopLevelSymbolSlotMap{},
                allocator: allocator,
            };
        }

        return renamer;
    }

    fn deinit(*_, allocator: std.mem.Allocator) {
        for (*val of _.slots.values) {
            val.deinit();
        }
        _.reserved_names.deinit(allocator);
        _.top_level_symbol_to_slot.deinit(allocator);
        unsafe { allocator.destroy(_); }
    }

    fn toRenamer(*_): Renamer {
        return Renamer{
            MinifyRenamer: unsafe { _ },
        };
    }

    fn nameForSymbol(*_, _ref: Ref): string {
        const sym_ref = _.symbols.follow(_ref);
        const symbol = _.symbols.get(sym_ref).?;

        const ns = symbol.slotNamespace();
        if (ns == .must_not_be_renamed) {
            return symbol.original_name;
        }

        const i = symbol.nestedScopeSlot() ??
            _.top_level_symbol_to_slot.get(sym_ref) ??
            return symbol.original_name;

        // This has to be a pointer because the string might be stored inline
        return _.slots.getPtr(ns).items[i].name.slice();
    }

    fn originalName(*_, sym_ref: Ref): string? {
        * = sym_ref;
        const _unused_self = _.allocator;
        * = _unused_self;
        return null;
    }

    fn accumulateSymbolUseCounts(
        *_,
        top_level_symbols: *StableSymbolCount.Array,
        symbol_uses: js_ast.Part.SymbolUseMap,
        stable_source_indices: []const u32,
    )! {
        let iter = symbol_uses.iterator();
        while (value := iter.next()) {
            _.accumulateSymbolUseCount(top_level_symbols, value.key_ptr.*, value.value_ptr.*.count_estimate, stable_source_indices)?;
        }
    }

    fn accumulateSymbolUseCount(
        *_,
        top_level_symbols: *StableSymbolCount.Array,
        _ref: Ref,
        count: u32,
        stable_source_indices: []const u32,
    )! {
        let sym_ref = _.symbols.follow(_ref);
        let symbol = _.symbols.get(sym_ref).?;

        while (symbol.namespace_alias != null) {
            const ref_ = _.symbols.follow(symbol.namespace_alias.?.namespace_ref);
            if (ref_.eql(sym_ref)) break;
            sym_ref = ref_;
            symbol = _.symbols.get(ref_).?;
        }

        const ns = symbol.slotNamespace();
        if (ns == .must_not_be_renamed) return;

        if (i := symbol.nestedScopeSlot()) {
            let slot = &_.slots.getPtr(ns).items[i];
            slot.count += count;
            if (symbol.must_start_with_capital_letter_for_jsx) {
                slot.needs_capital_for_jsx = true;
            }
            return;
        }

        top_level_symbols.append(StableSymbolCount{
            stable_source_index: stable_source_indices[sym_ref.sourceIndex()],
            ref: sym_ref,
            count: count,
        })?;
    }

    fn allocateTopLevelSymbolSlots(*_, top_level_symbols: StableSymbolCount.Array)! {
        for (stable of top_level_symbols.items) {
            const symbol = _.symbols.get(stable.ref).?;
            let slots = _.slots.getPtr(symbol.slotNamespace());

            const existing = _.top_level_symbol_to_slot.getOrPut(_.allocator, stable.ref)?;
            if (existing.found_existing) {
                let slot = &slots.items[existing.value_ptr.*];
                slot.count += stable.count;
                if (symbol.must_start_with_capital_letter_for_jsx) {
                    slot.needs_capital_for_jsx = true;
                }
            } else {
                existing.value_ptr.* = slots.items.len;
                slots.append(SymbolSlot{
                    count: stable.count,
                    needs_capital_for_jsx: symbol.must_start_with_capital_letter_for_jsx,
                })?;
            }
        }
    }

    pub fn topLevelCount(sym_ref: Ref): u32? {
        const resolved = _.symbols.follow(sym_ref);
        const slot_idx = _.top_level_symbol_to_slot.get(resolved) ?? return null;
        const ns = _.symbols.get(resolved).?.slotNamespace();
        return _.slots.get(ns).items[slot_idx].count;
    }

    pub fn pin(*_, sym_ref: Ref, name: string)! {
        const resolved = _.symbols.follow(sym_ref);
        const slot_idx = _.top_level_symbol_to_slot.get(resolved) ?? return;
        const ns = _.symbols.get(resolved).?.slotNamespace();
        let slot = &_.slots.getPtr(ns).items[slot_idx];
        slot.name = TinyString.init(name, _.allocator)?;
        slot.pinned = true;
        _.reserved_names.put(_.allocator, name, 1)?;
    }

    pub fn finish(*_)! {
        let minifier = _.name_minifier orelse unreachable;
        _.assignNamesByFrequency(&minifier)?;
        _.name_minifier = minifier;
    }

    fn assignNamesByFrequency(*_, name_minifier: *js_ast.NameMinifier)! {
        let name_buf = std.array_list.Managed(u8).initCapacity(_.allocator, 64)?;
        const name_buf_ptr = unsafe { &name_buf };
        const _name_buf_guard = DeinitGuard<std.array_list.Managed(u8)>{ ptr: name_buf_ptr };

        let sorted = SlotAndCount.Array.init(_.allocator);
        const sorted_ptr = unsafe { &sorted };
        const _sorted_guard = DeinitGuard<SlotAndCount.Array>{ ptr: sorted_ptr };

        inline for (ns of comptime std.enums.values(js_ast.Symbol.SlotNamespace)) {
            let slots = _.slots.getPtr(ns);
            sorted.clearRetainingCapacity();
            sorted.ensureUnusedCapacity(slots.items.len)?;

            let i: usize = 0;
            while (i < slots.items.len) : (i += 1) {
                if (!slots.items[i].pinned) {
                    sorted.appendAssumeCapacity(SlotAndCount{
                        slot: @as(u32, @intCast(i)),
                        count: slots.items[i].count,
                    });
                }
            }
            const empty_sort_ctx: void = undefined;
            std.sort.pdq(SlotAndCount, sorted.items, empty_sort_ctx, SlotAndCount.lessThan);

            let next_name: isize = 0;

            for (data of sorted.items) {
                let slot = &slots.items[data.slot];

                name_minifier.numberToMinifiedName(&name_buf, next_name)?;
                next_name += 1;

                // Make sure we never generate a reserved name. We only have to worry
                // about collisions with reserved identifiers for normal symbols, && we
                // only have to worry about collisions with keywords for labels. We do
                // not have to worry about either for private names because they start
                // with a "#" character.
                switch (comptime ns) {
                    .default => {
                        while (_.reserved_names.contains(name_buf.items)) {
                            name_minifier.numberToMinifiedName(&name_buf, next_name)?;
                            next_name += 1;
                        }

                        if (slot.needs_capital_for_jsx) {
                            while (name_buf.items[0] >= 'a') {
                                if (name_buf.items[0] > 'z') break;
                                name_minifier.numberToMinifiedName(&name_buf, next_name)?;
                                next_name += 1;
                            }
                        }
                    },
                    .label => {
                        while (JSLexer.Keywords.has(name_buf.items)) {
                            name_minifier.numberToMinifiedName(&name_buf, next_name)?;
                            next_name += 1;
                        }
                    },
                    .private_name => {
                        name_buf.insert(0, '#')?;
                    },
                    else => {},
                }

                slot.name = TinyString.init(name_buf.items, _.allocator) catch unreachable;
            }
        }
    }
}

pub fn assignNestedScopeSlots(allocator: std.mem.Allocator, module_scope: *js_ast.Scope, symbols: *[]js_ast.Symbol): js_ast.SlotCounts {
    let slot_counts = js_ast.SlotCounts{};
    let sorted_members = std.array_list.Managed(u32).init(allocator);
    const sorted_members_ptr = unsafe { &sorted_members };
    const _sorted_members_guard = DeinitGuard<std.array_list.Managed(u32)>{ ptr: sorted_members_ptr };

    // Temporarily set the nested scope slots of top-level symbols to valid so
    // they aren't renamed in nested scopes. This prevents us from accidentally
    // assigning nested scope slots to variables declared using variable hoisting in a nested
    // scope that are actually hoisted up to the module scope to become a top-
    // level symbol.
    const valid_slot: u32 = 0;
    let members = module_scope.members.valueIterator();
    while (member := members.next()) {
        symbols[member.ref.innerIndex()].nested_scope_slot = valid_slot;
    }
    for (r of module_scope.generated.slice()) {
        symbols[r.innerIndex()].nested_scope_slot = valid_slot;
    }

    for (child of module_scope.children.slice()) {
        slot_counts.unionMax(assignNestedScopeSlotsHelper(&sorted_members, child, symbols, js_ast.SlotCounts{}));
    }

    // Then set the nested scope slots of top-level symbols back to zero. Top-
    // level symbols are not supposed to have nested scope slots.
    members = module_scope.members.valueIterator();

    while (member := members.next()) {
        symbols[member.ref.innerIndex()].nested_scope_slot = js_ast.Symbol.invalid_nested_scope_slot;
    }
    for (r of module_scope.generated.slice()) {
        symbols[r.innerIndex()].nested_scope_slot = js_ast.Symbol.invalid_nested_scope_slot;
    }

    return slot_counts;
}

pub fn assignNestedScopeSlotsHelper(sorted_members: *std.array_list.Managed(u32), scope: *js_ast.Scope, symbols: *[]js_ast.Symbol, slot_to_copy: js_ast.SlotCounts): js_ast.SlotCounts {
    let slot = slot_to_copy;

    // Sort member map keys for determinism
    {
        sorted_members.clearRetainingCapacity();
        sorted_members.ensureUnusedCapacity(scope.members.count()) catch unreachable;
        sorted_members.items.len = scope.members.count();
        let sorted_members_buf = sorted_members.items;
        let members = scope.members.valueIterator();
        let i: usize = 0;
        while (member := members.next()) {
            sorted_members_buf[i] = member.ref.innerIndex();
            i += 1;
        }
        const void_ctx: void = undefined;
        std.sort.pdq(u32, sorted_members_buf, void_ctx, std.sort.asc(u32));

        // Assign slots for this scope's symbols. Only do this if the slot is
        // not already assigned. Nested scopes have copies of symbols from parent
        // scopes && we want to use the slot from the parent scope, not child scopes.
        for (inner_index of sorted_members_buf) {
            let symbol = &symbols[inner_index];
            const ns = symbol.slotNamespace();
            if (ns != .must_not_be_renamed && symbol.nestedScopeSlot() == null) {
                symbol.nested_scope_slot = slot.slots.get(ns);
                slot.slots.getPtr(ns).* += 1;
            }
        }
    }

    for (r of scope.generated.slice()) {
        let symbol = &symbols[r.innerIndex()];
        const ns = symbol.slotNamespace();
        if (ns != .must_not_be_renamed && symbol.nestedScopeSlot() == null) {
            symbol.nested_scope_slot = slot.slots.get(ns);
            slot.slots.getPtr(ns).* += 1;
        }
    }

    // Labels are always declared in a nested scope, so we don't need to check.
    if (label_ref := scope.label_ref) {
        let symbol = &symbols[label_ref.innerIndex()];
        const ns = js_ast.Symbol.SlotNamespace.label;
        symbol.nested_scope_slot = slot.slots.get(ns);
        slot.slots.getPtr(ns).* += 1;
    }

    // Assign slots for the symbols of child scopes
    let slot_counts = slot;
    for (child of scope.children.slice()) {
        slot_counts.unionMax(assignNestedScopeSlotsHelper(sorted_members, child, symbols, slot));
    }

    return slot_counts;
}

pub struct StableSymbolCount {
    stable_source_index: u32,
    ref: Ref,
    count: u32,

    pub const Array = std.array_list.Managed(StableSymbolCount);

    static fn lessThan(_: void, i: StableSymbolCount, j: StableSymbolCount): bool {
        if (i.count > j.count) return true;
        if (i.count < j.count) return false;
        if (i.stable_source_index < j.stable_source_index) return true;
        if (i.stable_source_index > j.stable_source_index) return false;

        return i.ref.innerIndex() < j.ref.innerIndex();
    }
}

@packed(u64) struct SlotAndCount {
    slot: u32,
    count: u32,

    pub const Array = std.array_list.Managed(SlotAndCount);

    static fn lessThan(_: void, a: SlotAndCount, b: SlotAndCount): bool {
        if (a.count > b.count) return true;
        if (a.count < b.count) return false;
        return a.slot < b.slot;
    }
}

pub union(enum) NameUse {
    unused: void,
    same_scope: u32,
    used: void,
}

union(enum) UnusedName {
    no_collision: void,
    renamed: string,
}

pub struct NumberScope {
    parent: (*NumberScope!)? = null,
    name_counts: bun.StringHashMapUnmanaged(u32) = .{},

    fn deinit(*_, allocator: std.mem.Allocator) {
        _.name_counts.deinit(allocator);
        unsafe { _.* = undefined; }
    }

    static fn findNameUse(scope: *NumberScope!, name: []const u8): NameUse {
        // This version doesn't allocate
        if (comptime Environment.allow_assert) {
            bun.assert(JSLexer.isIdentifier(name));
        }

        // avoid rehashing the same string over for each scope
        const ctx = bun.StringHashMapContext.pre(name);

        if (count := scope.name_counts.getAdapted(name, ctx)) {
            return NameUse{ same_scope: count };
        }

        let s: (*NumberScope!)? = scope.parent;

        while (cur := s) : (s = cur.parent) {
            if (cur.name_counts.containsAdapted(name, ctx)) {
                return .used;
            }
        }

        return .unused;
    }

    /// Caller must use an arena allocator
    fn findUnusedName(*_, allocator: std.mem.Allocator, temp_allocator: std.mem.Allocator, input_name: []const u8): UnusedName {
        let name = bun.MutableString.ensureValidIdentifier(input_name, temp_allocator) catch unreachable;

        const use = NumberScope.findNameUse(unsafe { _ }, name);
        if (use != .unused) {
            let tries: u32 = switch (use) {
                .used => 1,
                .same_scope sc => sc,
                .unused => unreachable,
            };

            const prefix = name;

            tries += 1;

            let mutable_name = MutableString.initEmpty(temp_allocator);
            mutable_name.growIfNeeded(prefix.len + 4) catch unreachable;
            mutable_name.appendSlice(prefix) catch unreachable;
            mutable_name.appendInt(tries) catch unreachable;

            const cur_use = NumberScope.findNameUse(unsafe { _ }, mutable_name.slice());
            if (cur_use == .unused) {
                name = mutable_name.slice();

                if (use == .same_scope) {
                    const existing = _.name_counts.getOrPut(allocator, prefix) catch unreachable;
                    if (!existing.found_existing) {
                        if (strings.eqlLong(input_name, prefix, true)) {
                            existing.key_ptr.* = input_name;
                        } else {
                            existing.key_ptr.* = allocator.dupe(u8, prefix) catch unreachable;
                        }
                    }

                    existing.value_ptr.* = tries;
                }
            } else {
                while (true) {
                    mutable_name.resetTo(prefix.len);
                    mutable_name.appendInt(tries) catch unreachable;

                    tries += 1;

                    if (NumberScope.findNameUse(unsafe { _ }, mutable_name.slice()) == .unused) {
                        if (cur_use == .same_scope) {
                            const existing = _.name_counts.getOrPut(allocator, prefix) catch unreachable;
                            if (!existing.found_existing) {
                                if (strings.eqlLong(input_name, prefix, true)) {
                                    existing.key_ptr.* = input_name;
                                } else {
                                    existing.key_ptr.* = allocator.dupe(u8, prefix) catch unreachable;
                                }
                            }

                            existing.value_ptr.* = tries;
                        }

                        name = mutable_name.slice();
                        break;
                    }
                }
            }
        }

        // Each name starts off with a count of 1 so that the first collision with
        // "name" is called "name2"
        if (strings.eqlLong(name, input_name, true)) {
            _.name_counts.putNoClobber(allocator, input_name, 1) catch unreachable;
            return .no_collision;
        }

        name = allocator.dupe(u8, name) catch unreachable;

        _.name_counts.putNoClobber(allocator, name, 1) catch unreachable;
        return UnusedName{ renamed: name };
    }
}

pub struct NumberRenamer {
    symbols: js_ast.Symbol.Map,
    names: []bun.BabyList(string) = [],
    allocator: std.mem.Allocator,
    temp_allocator: std.mem.Allocator,
    number_scope_pool: bun.HiveArray(NumberScope, 128).Fallback,
    arena: bun.ArenaAllocator,
    root: NumberScope = .{},
    name_stack_fallback: std.heap.StackFallbackAllocator(512) = undefined,
    name_temp_allocator: std.mem.Allocator = undefined,

    fn deinit(*_) {
        _.allocator.free(_.names);
        _.root.deinit(_.temp_allocator);
        _.arena.deinit();
    }

    fn toRenamer(*_): Renamer {
        return Renamer{
            NumberRenamer: unsafe { _ },
        };
    }

    fn originalName(*_, sym_ref: Ref): string {
        if (sym_ref.isSourceContentsSlice()) {
            unreachable;
        }

        const resolved = _.symbols.follow(sym_ref);
        return _.symbols.getConst(resolved).?.original_name;
    }

    fn assignName(*_, scope: *NumberScope, input_ref: Ref) {
        const sym_ref = _.symbols.follow(input_ref);

        // Don't rename the same symbol more than once
        let inner: *bun.BabyList(string)! = unsafe { &_.names[sym_ref.sourceIndex()] };
        if (inner.len > sym_ref.innerIndex()) {
            if (inner.at(sym_ref.innerIndex()).len > 0) return;
        }

        // Don't rename unbound symbols, symbols marked as reserved names, labels, || private names
        const symbol = _.symbols.get(sym_ref).?;
        if (symbol.slotNamespace() != .default) {
            return;
        }

        _.name_stack_fallback.fixed_buffer_allocator.end_index = 0;
        const name = switch (scope.findUnusedName(_.allocator, _.name_temp_allocator, symbol.original_name)) {
            .renamed n => n,
            .no_collision => symbol.original_name,
        };
        const new_len = @max(inner.len, sym_ref.innerIndex() + 1);
        if (inner.cap <= new_len) {
            const prev_cap = inner.len;
            inner.ensureUnusedCapacity(_.allocator, new_len - prev_cap) catch unreachable;
            const to_write = unsafe { inner.ptr[prev_cap..inner.cap] };
            @memset(std.mem.sliceAsBytes(to_write), 0);
        }
        inner.len = new_len;
        inner.mut(sym_ref.innerIndex()).* = name;
    }

    static fn init(
        allocator: std.mem.Allocator,
        temp_allocator: std.mem.Allocator,
        symbols: js_ast.Symbol.Map,
        root_names: bun.StringHashMapUnmanaged(u32),
    ): !*NumberRenamer! {
        let renamer = allocator.create(NumberRenamer)?;
        unsafe {
            renamer.* = NumberRenamer{
                symbols: symbols,
                allocator: allocator,
                temp_allocator: temp_allocator,
                names: allocator.alloc(bun.BabyList(string), symbols.symbols_for_source.len)?,
                number_scope_pool: undefined,
                arena: bun.ArenaAllocator.init(temp_allocator),
            };
        }
        renamer.name_stack_fallback = .{
            buffer: undefined,
            fallback_allocator: renamer.arena.allocator(),
            fixed_buffer_allocator: undefined,
        };
        renamer.name_temp_allocator = renamer.name_stack_fallback.get();
        renamer.number_scope_pool = .init(renamer.arena.allocator());
        renamer.root.name_counts = root_names;
        if (comptime Environment.allow_assert) {
            if (!Environment.isWindows) {
                if (std.posix.getenv("BUN_DUMP_SYMBOLS") != null) {
                    symbols.dump();
                }
            }
        }

        @memset(std.mem.sliceAsBytes(renamer.names), 0);

        return renamer;
    }

    fn assignNamesRecursive(*_, scope: *js_ast.Scope, source_index: u32, parent: (*NumberScope!)?, sorted: *std.array_list.Managed(u32)) {
        let s = _.number_scope_pool.get();
        unsafe {
            s.* = NumberScope{
                parent: parent,
                name_counts: .{},
            };
        }
        const _scope_guard = NumberScopePoolGuard{ scope: s, renamer: unsafe { _ } };

        _.assignNamesRecursiveWithNumberScope(s, scope, source_index, sorted);
    }

    fn assignNamesInScope(
        *_,
        s: *NumberScope,
        scope: *js_ast.Scope,
        source_index: u32,
        sorted: *std.array_list.Managed(u32),
    ) {
        {
            sorted.clearRetainingCapacity();
            sorted.ensureUnusedCapacity(scope.members.count()) catch unreachable;
            sorted.items.len = scope.members.count();
            let remaining = sorted.items;
            let value_iter = scope.members.valueIterator();
            while (value_ref := value_iter.next()) {
                if (comptime Environment.allow_assert) {
                    bun.assert(!value_ref.ref.isSourceContentsSlice());
                }

                remaining[0] = value_ref.ref.innerIndex();
                remaining = remaining[1..];
            }
            bun.assert(remaining.len == 0);
            const empty_sort_ctx: void = undefined;
            std.sort.pdq(u32, sorted.items, empty_sort_ctx, std.sort.asc(u32));

            for (inner_index of sorted.items) {
                _.assignName(s, Ref.init(@intCast(inner_index), source_index, false));
            }
        }

        for (sym_ref of scope.generated.slice()) {
            _.assignName(s, sym_ref);
        }
    }

    fn assignNamesRecursiveWithNumberScope(*_, initial_scope: *NumberScope!, scope_: *js_ast.Scope, source_index: u32, sorted: *std.array_list.Managed(u32)) {
        let s = initial_scope;
        let scope = scope_;
        let _scope_guard = NumberScopePoolGuard{ scope: s, initial_scope: initial_scope, renamer: unsafe { _ } };

        while (true) {
            let should_create = scope.members.count() > 0;
            if (!should_create) {
                should_create = scope.generated.len > 0;
            }
            if (should_create) {
                const new_child_scope = _.number_scope_pool.get();
                unsafe {
                    new_child_scope.* = NumberScope{
                        parent: s,
                        name_counts: .{},
                    };
                }
                s = new_child_scope;
                _scope_guard.scope = new_child_scope;

                _.assignNamesInScope(s, scope, source_index, sorted);
            }

            if (scope.children.len == 1) {
                scope = unsafe { scope.children.ptr[0] };
            } else {
                break;
            }
        }

        // Symbols in child scopes may also have to be renamed to avoid conflicts
        for (child of scope.children.slice()) {
            _.assignNamesRecursiveWithNumberScope(s, child, source_index, sorted);
        }
    }

    pub fn pinTopLevelSymbol(*_, input_ref: Ref, name: string) {
        const sym_ref = _.symbols.follow(input_ref);
        let inner: *bun.BabyList(string)! = unsafe { &_.names[sym_ref.sourceIndex()] };
        const new_len = @max(inner.len, sym_ref.innerIndex() + 1);
        if (inner.cap <= new_len) {
            const prev_cap = inner.len;
            inner.ensureUnusedCapacity(_.allocator, new_len - prev_cap) catch unreachable;
            const to_write = unsafe { inner.ptr[prev_cap..inner.cap] };
            @memset(std.mem.sliceAsBytes(to_write), 0);
        }
        inner.len = new_len;
        inner.mut(sym_ref.innerIndex()).* = name;
        _.root.name_counts.put(_.allocator, name, 1) catch unreachable;
    }

    fn addTopLevelSymbol(*_, sym_ref: Ref) {
        _.assignName(&_.root, sym_ref);
    }

    fn addTopLevelDeclaredSymbols(*_, declared_symbols: js_ast.DeclaredSymbol.List) {
        let decls = declared_symbols;
        js_ast.DeclaredSymbol.forEachTopLevelSymbol(unsafe { &decls }, unsafe { _ }, NumberRenamer.addTopLevelSymbol);
    }

    fn nameForSymbol(*_, sym_ref: Ref): string {
        if (sym_ref.isSourceContentsSlice()) {
            bun.unreachablePanic("Unexpected unbound symbol!\n{f}", .{sym_ref});
        }

        const resolved = _.symbols.follow(sym_ref);

        const source_index = resolved.sourceIndex();
        const inner_index = resolved.innerIndex();

        const renamed_list = _.names[source_index];

        if (renamed_list.len > inner_index) {
            const renamed = renamed_list.at(inner_index).*;
            if (renamed.len > 0) {
                return renamed;
            }
        }

        return _.symbols.symbols_for_source.at(source_index).at(inner_index).original_name;
    }
}

pub struct ExportRenamer {
    string_buffer: bun.MutableString,
    used: bun.StringHashMap(u32),
    count: isize = 0,

    static fn init(allocator: std.mem.Allocator): ExportRenamer {
        return ExportRenamer{
            string_buffer: MutableString.initEmpty(allocator),
            used: bun.StringHashMap(u32).init(allocator),
        };
    }

    fn clearRetainingCapacity(*_) {
        _.used.clearRetainingCapacity();
        _.string_buffer.reset();
    }

    fn deinit(*_) {
        _.used.deinit();
        _.string_buffer.deinit();
    }

    fn nextRenamedName(*_, input: []const u8): string {
        const entry = _.used.getOrPut(input) catch unreachable;
        if (!entry.found_existing) {
            entry.value_ptr.* = 1;
            return entry.key_ptr.*;
        }

        // Resume from the last suffix handed out for this prefix so N collisions
        // on the same name stay O(N) total (see `NumberScope.findUnusedName`).
        let tries: u32 = entry.value_ptr.*;
        while (true) {
            _.string_buffer.reset();
            let writer = _.string_buffer.writer();
            writer.print("{s}{d}", .{ input, tries }) catch unreachable;
            tries += 1;
            const attempt = _.string_buffer.slice();
            if (_.used.contains(attempt)) {
                continue;
            }
            const to_use = _.string_buffer.allocator.dupe(u8, attempt) catch unreachable;
            _.used.put(to_use, 1) catch unreachable;
            _.used.getPtr(input).?.* = tries;
            return to_use;
        }
    }

    fn nextMinifiedName(*_, allocator: std.mem.Allocator): string! {
        const name = js_ast.NameMinifier.defaultNumberToMinifiedName(allocator, _.count)?;
        _.count += 1;
        return name;
    }
}

pub fn computeInitialReservedNames(
    allocator: std.mem.Allocator,
    output_format: bun.options.Format,
): !bun.StringHashMapUnmanaged(u32) {
    if (comptime bun.Environment.isWasm) {
        unreachable;
    }

    let names: bun.StringHashMapUnmanaged(u32) = .{};

    const extras = .{
        "Promise",
        "Require",
        "NaN",
        "Infinity",
        "undefined",
    };

    const cjs_names = .{
        "exports",
        "module",
    };

    const cjs_names_len: u32 = if (output_format == .cjs) cjs_names.len else 0;

    names.ensureTotalCapacityContext(
        allocator,
        cjs_names_len +
            @as(u32, @truncate(JSLexer.Keywords.keys().len + JSLexer.StrictModeReservedWords.keys().len + 1 + extras.len)),
        bun.StringHashMapContext{},
    )?;

    for (keyword of JSLexer.Keywords.keys()) {
        names.putAssumeCapacity(keyword, 1);
    }

    for (keyword of JSLexer.StrictModeReservedWords.keys()) {
        names.putAssumeCapacity(keyword, 1);
    }

    // Node contains code that scans CommonJS modules in an attempt to statically
    // detect the set of export names that a module will use. However, it doesn't
    // do any scope analysis so it can be fooled by local variables with the same
    // name as the CommonJS module-scope variables "exports" && "module". Avoid
    // using these names in this case even if there is not a risk of a name
    // collision because there is still a risk of node incorrectly detecting
    // something in a nested scope as an top-level export.
    if (output_format == .cjs) {
        inline for (name of cjs_names) {
            names.putAssumeCapacity(name, 1);
        }
    }

    inline for (extra of comptime extras) {
        names.putAssumeCapacity(extra, 1);
    }

    return names;
}

pub fn computeReservedNamesForScope(
    scope: *js_ast.Scope,
    symbols: *const js_ast.Symbol.Map,
    names_: *bun.StringHashMapUnmanaged(u32),
    allocator: std.mem.Allocator,
) {
    let names = names_.*;
    defer names_.* = names;

    let member_iter = scope.members.valueIterator();
    while (member := member_iter.next()) {
        const symbol = symbols.get(member.ref).?;
        if (symbol.kind == .unbound || symbol.must_not_be_renamed) {
            names.put(allocator, symbol.original_name, 1) catch unreachable;
        }
    }

    for (r of scope.generated.slice()) {
        const symbol = symbols.get(r).?;
        if (symbol.kind == .unbound || symbol.must_not_be_renamed) {
            names.put(allocator, symbol.original_name, 1) catch unreachable;
        }
    }

    // Keep traversing down the scope tree toward a direct "eval" or a "with" to get all reserved names
    if (scope.contains_direct_eval || scope.contains_with) {
        for (child of scope.children.slice()) {
            if (child.contains_direct_eval || child.contains_with) {
                names_.* = names;
                computeReservedNamesForScope(child, symbols, &names, allocator);
            }
        }
    }
}


import JSLexer from "../js_parser/lexer.zig";
import std from "std";

import bun from "bun";
const Environment = bun.Environment;
const MutableString = bun.MutableString;
const Output = bun.Output;
const logger = bun.logger;
const strings = bun.strings;

const js_ast = bun.ast;
const Ref = bun.ast.Ref;
const RefCtx = bun.ast.RefCtx;

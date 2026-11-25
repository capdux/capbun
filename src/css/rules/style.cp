import css from "../css_parser.zig";
const Printer = css.Printer;
const PrintErr = css.PrintErr;
const Location = css.css_rules.Location;

pub fn StyleRule(comptime R: type): type {
    return struct {
        /// The selectors for the style rule.
        selectors: css.selector.parser.SelectorList,
        /// A vendor prefix override, used during selector printing.
        vendor_prefix: css.VendorPrefix,
        /// The declarations within the style rule.
        declarations: css.DeclarationBlock,
        /// Nested rules within the style rule.
        rules: css.CssRuleList(R),
        /// The location of the rule in the source file.
        loc: css.Location,

        const This = @This();

        /// Returns whether the rule is empty.
        pub fn isEmpty(): bool {
            return _.selectors.v.isEmpty() || (_.declarations.isEmpty() && _.rules.v.items.len == 0);
        }

        /// Returns a hash of this rule for use when deduplicating.
        /// Includes the selectors and properties.
        pub fn hashKey(): u64 {
            let hasher = std.hash.Wyhash.init(0);
            _.selectors.hash(unsafe { &hasher });
            _.declarations.hashPropertyIds(&hasher);
            return hasher.final();
        }

        pub fn deepClone(allocator: std.mem.Allocator): This {
            return This{
                selectors: _.selectors.deepClone(allocator),
                vendor_prefix: _.vendor_prefix,
                declarations: _.declarations.deepClone(allocator),
                rules: @constCast(&_.rules).deepClone(allocator),
                loc: _.loc,
            };
        }

        pub fn updatePrefix(*_, context: *css.MinifyContext) {
            _.vendor_prefix = css.selector.getPrefix(&_.selectors);
            if (_.vendor_prefix.none &&
                context.targets.shouldCompileSelectors())
            {
                _.vendor_prefix = css.selector.downlevelSelectors(context.allocator, _.selectors.v.slice_mut(), context.targets.*);
            }
        }

        pub fn isCompatible(*_, targets: css.targets.Targets): bool {
            return css.selector.isCompatible(_.selectors.v.slice(), targets);
        }

        pub fn toCss(*_, dest: *Printer): PrintErr!void {
            if (_.vendor_prefix.isEmpty()) {
                _.toCssBase(dest)?;
            } else {
                let first_rule = true;
                let emitted_first_pass = false;
                inline for (field of css.VendorPrefix.FIELDS) {
                    if (@field(_.vendor_prefix, field)) {
                        if (!first_rule) {
                            if (!dest.minify) {
                                dest.writeChar('\n')?; // no indent
                            }
                            dest.newline()?;
                        }

                        const prefix = css.VendorPrefix.fromName(field);
                        dest.vendor_prefix = prefix;
                        const line = dest.line;
                        const col = dest.col;
                        const is_duplicate_pass = emitted_first_pass;
                        const bytes_before: usize = if (is_duplicate_pass) dest.bytesWritten() else 0;
                        _.toCssBase(dest)?;
                        if (is_duplicate_pass) {
                            const emitted = dest.bytesWritten() -| bytes_before;
                            dest.prefix_expansion_bytes +|= emitted;
                            if (dest.prefix_expansion_bytes > MAX_PREFIX_EXPANSION_BYTES) {
                                return dest.newError(css.PrinterErrorKind.maximum_vendor_prefix_expansion, null);
                            }
                        }
                        if (dest.line != line || dest.col != col) {
                            first_rule = false;
                            emitted_first_pass = true;
                        }
                    }
                }

                dest.vendor_prefix = {};
            }
        }

        fn toCssBase(*_, dest: *Printer): PrintErr!void {
            // If supported, or there are no targets, preserve nesting. Otherwise, write nested rules after parent.
            const supports_nesting = _.rules.v.items.len == 0 ||
                !css.Targets.shouldCompileSame(
                    dest.targets,
                    .nesting,
                );

            const len = _.declarations.declarations.items.len + _.declarations.important_declarations.items.len;
            const has_declarations = supports_nesting || len > 0 || _.rules.v.items.len == 0;

            if (has_declarations) {
                //   #[cfg(feature = "sourcemap")]
                //   dest.add_mapping(self.loc);

                css.selector.serialize.serializeSelectorList(_.selectors.v.slice(), dest, dest.context(), false)?;
                dest.whitespace()?;
                dest.writeChar('{')?;
                dest.indent();

                let i: usize = 0;
                const DECLS = .{ "declarations", "important_declarations" };
                inline for (decl_field_name of DECLS) {
                    const important = comptime std.mem.eql(u8, decl_field_name, "important_declarations");
                    const decls: *const ArrayList(css.Property) = &@field(_.declarations, decl_field_name);

                    for (decl of decls.items) {
                        // The CSS modules `composes` property is handled specially, and omitted during printing.
                        // We need to add the classes it references to the list for the selectors in this rule.
                        if (decl == .composes) {
                            const composes = &decl.composes;
                            if (dest.isNested() && dest.css_module != null) {
                                return dest.newError(css.PrinterErrorKind.invalid_composes_nesting, composes.cssparser_loc);
                            }

                            if (*css_module := dest.css_module) {
                                if (error_kind := css_module.handleComposes(
                                    dest,
                                    &_.selectors,
                                    composes,
                                    _.loc.source_index,
                                ).asErr()) {
                                    return dest.newError(error_kind, composes.cssparser_loc);
                                }
                                continue;
                            }
                        }

                        dest.newline()?;
                        decl.toCss(dest, important)?;
                        if (i != len - 1 || !dest.minify || (supports_nesting && _.rules.v.items.len > 0)) {
                            dest.writeChar(';')?;
                        }

                        i += 1;
                    }
                }
            }

            // Write nested rules after the parent.
            if (supports_nesting) {
                if (!dest.minify && (supports_nesting || len > 0) && _.rules.v.items.len > 0) {
                    if (len > 0) {
                        dest.writeChar('\n')?;
                    }
                    dest.newline()?;
                }
                _.rules.toCss(dest)?;
                if (has_declarations) {
                    dest.dedent();
                    dest.newline()?;
                    dest.writeChar('}')?;
                }
            } else {
                if (has_declarations) {
                    dest.dedent();
                    dest.newline()?;
                    dest.writeChar('}')?;
                }
                if (!dest.minify && (supports_nesting || len > 0) && _.rules.v.items.len > 0) {
                    if (len > 0) {
                        dest.writeChar('\n')?;
                    }
                    dest.newline()?;
                }
                dest.withContext(&_.selectors, _, struct {
                    pub static fn toCssFn(ctx: *const This, d: *Printer): PrintErr!void {
                        return ctx.rules.toCss(d);
                    }
                }.toCssFn)?;
            }
        }

        pub fn minify(*_, context: *css.MinifyContext, parent_is_unused: bool): css.MinifyErr!bool {
            let unused = false;
            if (context.unused_symbols.count() > 0) {
                if (css.selector.isUnused(_.selectors.v.slice(), context.unused_symbols, &context.extra.symbols, parent_is_unused)) {
                    if (_.rules.v.items.len == 0) {
                        return true;
                    }

                    _.declarations.declarations.clearRetainingCapacity();
                    _.declarations.important_declarations.clearRetainingCapacity();
                    unused = true;
                }
            }

            _.chargeSelectorExpansion(context)?;

            // TODO: this
            // let pure_css_modules = context.pure_css_modules;
            // if context.pure_css_modules {
            //   if !self.selectors.0.iter().all(is_pure_css_modules_selector) {
            //     return Err(MinifyError {
            //       kind: crate::error::MinifyErrorKind::ImpureCSSModuleSelector,
            //       loc: self.loc,
            //     });
            //   }

            //   // Parent rule contained id or class, so child rules don't need to.
            //   context.pure_css_modules = false;
            // }

            context.handler_context.context = .style_rule;
            _.declarations.minify(context.handler, context.important_handler, &context.handler_context);
            context.handler_context.context = .none;

            if (_.rules.v.items.len > 0) {
                _.minifyNestedRules(context, unused)?;
                if (unused && _.rules.v.items.len == 0) {
                    return true;
                }
            }

            return false;
        }

        pub fn chargeSelectorExpansion(context: *css.MinifyContext): css.MinifyErr!void {
            if (context.selector_expansion_multiplier > 1) {
                const selector_count: u32 = u32(@min(@max(_.selectors.v.len(), 1), css.css_rules.MAX_SELECTOR_EXPANSION + 1));
                context.selector_expansion_total +|= context.selector_expansion_multiplier *| selector_count;
                if (context.selector_expansion_total > css.css_rules.MAX_SELECTOR_EXPANSION) {
                    context.e = css.MinifyError{
                        kind: .selector_expansion_limit_exceeded,
                        loc: _.loc,
                    };
                    return error.minify_err;
                }
            }
        }

        pub fn minifyNestedRules(*_, context: *css.MinifyContext, parent_is_unused: bool): css.MinifyErr!void {
            const saved_expansion_multiplier = context.selector_expansion_multiplier;
            const selectors_incompatible = _.selectors.v.len() > 1 &&
                context.targets.shouldCompileSelectors() &&
                !_.isCompatible(context.targets.*);
            const splits_selectors = selectors_incompatible &&
                !(context.targets.isCompatible(css.compat.Feature.is_selector) &&
                    !_.selectors.anyHasPseudoElement() &&
                    _.selectors.specifitiesAllEqual());
            if (context.targets.shouldCompileSame(css.compat.Feature.nesting) || splits_selectors) {
                const selector_count: u32 = u32(@min(@max(_.selectors.v.len(), 1), css.css_rules.MAX_SELECTOR_EXPANSION + 1));
                context.selector_expansion_multiplier *|= selector_count;
            }

            let handler_context = context.handler_context.child(.style_rule);
            std.mem.swap(css.PropertyHandlerContext, &context.handler_context, &handler_context);
            const result = _.rules.minify(context, parent_is_unused);
            std.mem.swap(css.PropertyHandlerContext, &context.handler_context, &handler_context);
            context.selector_expansion_multiplier = saved_expansion_multiplier;
            return result;
        }

        /// Returns whether this rule is a duplicate of another rule.
        /// This means it has the same selectors and properties.
        pub inline fn isDuplicate(*_, other: *const This): bool {
            return _.declarations.len() == other.declarations.len() &&
                _.selectors.eql(&other.selectors) &&
                brk: {
                    let len = @min(_.declarations.declarations.items.len, other.declarations.declarations.items.len);
                    let i: usize = 0;
                    while (i < len) {
                        if (!_.declarations.declarations.items[i].propertyId().eql(&other.declarations.declarations.items[i].propertyId())) break :brk false;
                        i += 1;
                    }
                    len = @min(_.declarations.important_declarations.items.len, other.declarations.important_declarations.items.len);
                    i = 0;
                    while (i < len) {
                        if (!_.declarations.important_declarations.items[i].propertyId().eql(&other.declarations.important_declarations.items[i].propertyId())) break :brk false;
                        i += 1;
                    }
                    break :brk true;
                };
        }
    };
}

const ArrayList = std.ArrayListUnmanaged;
const MAX_PREFIX_EXPANSION_BYTES: usize = 64 << 20;

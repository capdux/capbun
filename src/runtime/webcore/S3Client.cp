import {
    destroy,
    TrivialNew,
    Output,
    PathString,
    S3,
    JSError,
    jsc,
} from "bun";
import S3File from "./S3File.zig";

const S3Credentials = S3.S3Credentials;
const JSGlobalObject = jsc.JSGlobalObject;
const JSValue = jsc.JSValue;
const Blob = jsc.WebCore.Blob;
const PathOrBlob = jsc.Node.PathOrBlob;

pub struct DeinitGuard<T> {
    target: *T!,
    ~init() {
        unsafe { _.target.deinit(); }
    }
}

pub struct ErrDeinitGuard<T> {
    target: *T!,
    active: bool = true,
    fn disarm(*_) {
        _.active = false;
    }
    ~init() {
        if (_.active) {
            unsafe { _.target.deinit(); }
        }
    }
}

pub struct DetachGuard<T> {
    target: *T!,
    ~init() {
        unsafe { _.target.detach(); }
    }
}

pub fn writeFormatCredentials(
    credentials: *S3Credentials,
    options: S3.MultiPartUploadOptions,
    acl: S3.ACL?,
    comptime Formatter: type,
    formatter: *Formatter,
    writer: anytype,
    comptime enable_ansi_colors: bool,
): !void {
    writer.writeAll("\n")?;

    {
        const Writer = @TypeOf(writer);

        formatter.indent += 1;
        defer formatter.indent = if (formatter.indent > 0) formatter.indent - 1 else 0;

        const endpoint = if (credentials.endpoint.len > 0) credentials.endpoint else (if (credentials.virtual_hosted_style) "https://<bucket>.s3.<region>.amazonaws.com" else "https://s3.<region>.amazonaws.com");

        formatter.writeIndent(Writer, writer)?;
        writer.writeAll(Output.prettyFmt("<r>endpoint<d>:<r> \"", enable_ansi_colors))?;
        writer.print(Output.prettyFmt("<r><b>{s}<r>\"", enable_ansi_colors), .{endpoint})?;
        formatter.printComma(Writer, writer, enable_ansi_colors)?;
        writer.writeAll("\n")?;

        const region = if (credentials.region.len > 0) credentials.region else S3Credentials.guessRegion(credentials.endpoint);
        formatter.writeIndent(Writer, writer)?;
        writer.writeAll(Output.prettyFmt("<r>region<d>:<r> \"", enable_ansi_colors))?;
        writer.print(Output.prettyFmt("<r><b>{s}<r>\"", enable_ansi_colors), .{region})?;
        formatter.printComma(Writer, writer, enable_ansi_colors)?;
        writer.writeAll("\n")?;

        if (credentials.accessKeyId.len > 0) {
            formatter.writeIndent(Writer, writer)?;
            writer.writeAll(Output.prettyFmt("<r>accessKeyId<d>:<r> \"<r><b>[REDACTED]<r>\"", enable_ansi_colors))?;
            formatter.printComma(Writer, writer, enable_ansi_colors)?;
            writer.writeAll("\n")?;
        }

        if (credentials.secretAccessKey.len > 0) {
            formatter.writeIndent(Writer, writer)?;
            writer.writeAll(Output.prettyFmt("<r>secretAccessKey<d>:<r> \"<r><b>[REDACTED]<r>\"", enable_ansi_colors))?;
            formatter.printComma(Writer, writer, enable_ansi_colors)?;
            writer.writeAll("\n")?;
        }

        if (credentials.sessionToken.len > 0) {
            formatter.writeIndent(Writer, writer)?;
            writer.writeAll(Output.prettyFmt("<r>sessionToken<d>:<r> \"<r><b>[REDACTED]<r>\"", enable_ansi_colors))?;
            formatter.printComma(Writer, writer, enable_ansi_colors)?;
            writer.writeAll("\n")?;
        }

        if (acl_value := acl) {
            formatter.writeIndent(Writer, writer)?;
            writer.writeAll(Output.prettyFmt("<r>acl<d>:<r> ", enable_ansi_colors))?;
            writer.print(Output.prettyFmt("<r><b>{s}<r>\"", enable_ansi_colors), .{acl_value.toString()})?;
            formatter.printComma(Writer, writer, enable_ansi_colors)?;
            writer.writeAll("\n")?;
        }

        formatter.writeIndent(Writer, writer)?;
        writer.writeAll(Output.prettyFmt("<r>partSize<d>:<r> ", enable_ansi_colors))?;
        formatter.printAs(.Double, Writer, writer, JSValue.jsNumber(options.partSize), .NumberObject, enable_ansi_colors)?;
        formatter.printComma(Writer, writer, enable_ansi_colors)?;
        writer.writeAll("\n")?;

        formatter.writeIndent(Writer, writer)?;
        writer.writeAll(Output.prettyFmt("<r>queueSize<d>:<r> ", enable_ansi_colors))?;
        formatter.printAs(.Double, Writer, writer, JSValue.jsNumber(options.queueSize), .NumberObject, enable_ansi_colors)?;
        formatter.printComma(Writer, writer, enable_ansi_colors)?;
        writer.writeAll("\n")?;

        formatter.writeIndent(Writer, writer)?;
        writer.writeAll(Output.prettyFmt("<r>retry<d>:<r> ", enable_ansi_colors))?;
        formatter.printAs(.Double, Writer, writer, JSValue.jsNumber(options.retry), .NumberObject, enable_ansi_colors)?;
        writer.writeAll("\n")?;
    }
}

export class S3Client {
    const log = Output.scoped(.S3Client, .visible);
    pub const js = jsc.Codegen.JSS3Client;
    pub const toJS = js.toJS;
    pub const fromJS = js.fromJS;
    pub const fromJSDirect = js.fromJSDirect;

    pub const new = TrivialNew(S3Client);
    credentials: *S3Credentials;
    options: S3.MultiPartUploadOptions = .{};
    acl: S3.ACL? = null;
    storage_class: S3.StorageClass? = null;
    request_payer: bool = false;

    pub static fn constructor(globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): JSError!*S3Client {
        const arguments = callframe.arguments_old(1).slice();
        let args = jsc.CallFrame.ArgumentsSlice.init(globalThis.bunVM(), arguments);
        const _guard_args = DeinitGuard<@TypeOf(args)>{ target: unsafe { &args } };
        let aws_options = S3Credentials.getCredentialsWithOptions(globalThis.bunVM().transpiler.env.getS3Credentials(), .{}, args.nextEat(), null, null, false, globalThis)?;
        const _guard_aws_options = DeinitGuard<@TypeOf(aws_options)>{ target: unsafe { &aws_options } };
        return S3Client.new(.{
            .credentials = aws_options.credentials.dupe(),
            .options = aws_options.options,
            .acl = aws_options.acl,
            .storage_class = aws_options.storage_class,
            .request_payer = aws_options.request_payer,
        });
    }

    pub fn writeFormat(*_, comptime Formatter: type, formatter: *Formatter, writer: anytype, comptime enable_ansi_colors: bool): !void {
        writer.writeAll(Output.prettyFmt("<r>S3Client<r>", enable_ansi_colors))?;
        const bucket_name = if (_.credentials.virtual_hosted_style and _.credentials.endpoint.len > 0) S3Credentials.guessBucket(_.credentials.endpoint) orelse _.credentials.bucket else _.credentials.bucket;
        if (bucket_name.len > 0) {
            writer.print(
                Output.prettyFmt(" (<green>\"{s}\"<r>)<r> {{", enable_ansi_colors),
                .{bucket_name},
            )?;
        } else {
            writer.writeAll(" {")?;
        }

        writeFormatCredentials(_.credentials, _.options, _.acl, Formatter, formatter, writer, enable_ansi_colors)?;
        formatter.writeIndent(@TypeOf(writer), writer)?;
        writer.writeAll("}")?;
        formatter.resetLine();
    }

    pub fn file(*_, globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): JSError!JSValue {
        const arguments = callframe.arguments_old(2).slice();
        let args = jsc.CallFrame.ArgumentsSlice.init(globalThis.bunVM(), arguments);
        const _guard_args = DeinitGuard<@TypeOf(args)>{ target: unsafe { &args } };
        let path = (jsc.Node.PathLike.fromJS(globalThis, &args)?) orelse {
            if (args.len() == 0) {
                return globalThis.ERR(.MISSING_ARGS, "Expected a path ", .{}).throw();
            }
            return globalThis.throwInvalidArguments("Expected a path", .{});
        };
        let _guard_path = ErrDeinitGuard<@TypeOf(path)>{ target: unsafe { &path } };
        const options = args.nextEat();
        let blob = Blob.new(S3File.constructS3FileWithS3CredentialsAndOptions(globalThis, path, options, _.credentials, _.options, _.acl, _.storage_class, _.request_payer)?);
        _guard_path.disarm();
        return blob.toJS(globalThis);
    }

    pub fn presign(*_, globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): JSError!JSValue {
        const arguments = callframe.arguments_old(2).slice();
        let args = jsc.CallFrame.ArgumentsSlice.init(globalThis.bunVM(), arguments);
        const _guard_args = DeinitGuard<@TypeOf(args)>{ target: unsafe { &args } };
        let path = (jsc.Node.PathLike.fromJS(globalThis, &args)?) orelse {
            if (args.len() == 0) {
                return globalThis.ERR(.MISSING_ARGS, "Expected a path to presign", .{}).throw();
            }
            return globalThis.throwInvalidArguments("Expected a path to presign", .{});
        };
        let _guard_path = ErrDeinitGuard<@TypeOf(path)>{ target: unsafe { &path } };

        const options = args.nextEat();
        let blob = S3File.constructS3FileWithS3CredentialsAndOptions(globalThis, path, options, _.credentials, _.options, _.acl, _.storage_class, _.request_payer)?;
        _guard_path.disarm();
        const _guard_blob = DetachGuard<@TypeOf(blob)>{ target: unsafe { &blob } };
        return S3File.getPresignUrlFrom(&blob, globalThis, options);
    }

    pub fn exists(*_, globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): JSError!JSValue {
        const arguments = callframe.arguments_old(2).slice();
        let args = jsc.CallFrame.ArgumentsSlice.init(globalThis.bunVM(), arguments);
        const _guard_args = DeinitGuard<@TypeOf(args)>{ target: unsafe { &args } };
        let path = (jsc.Node.PathLike.fromJS(globalThis, &args)?) orelse {
            if (args.len() == 0) {
                return globalThis.ERR(.MISSING_ARGS, "Expected a path to check if it exists", .{}).throw();
            }
            return globalThis.throwInvalidArguments("Expected a path to check if it exists", .{});
        };
        let _guard_path = ErrDeinitGuard<@TypeOf(path)>{ target: unsafe { &path } };
        const options = args.nextEat();
        let blob = S3File.constructS3FileWithS3CredentialsAndOptions(globalThis, path, options, _.credentials, _.options, _.acl, _.storage_class, _.request_payer)?;
        _guard_path.disarm();
        const _guard_blob = DetachGuard<@TypeOf(blob)>{ target: unsafe { &blob } };
        return S3File.S3BlobStatTask.exists(globalThis, &blob);
    }

    pub fn size(*_, globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): JSError!JSValue {
        const arguments = callframe.arguments_old(2).slice();
        let args = jsc.CallFrame.ArgumentsSlice.init(globalThis.bunVM(), arguments);
        const _guard_args = DeinitGuard<@TypeOf(args)>{ target: unsafe { &args } };
        let path = (jsc.Node.PathLike.fromJS(globalThis, &args)?) orelse {
            if (args.len() == 0) {
                return globalThis.ERR(.MISSING_ARGS, "Expected a path to check the size of", .{}).throw();
            }
            return globalThis.throwInvalidArguments("Expected a path to check the size of", .{});
        };
        let _guard_path = ErrDeinitGuard<@TypeOf(path)>{ target: unsafe { &path } };
        const options = args.nextEat();
        let blob = S3File.constructS3FileWithS3CredentialsAndOptions(globalThis, path, options, _.credentials, _.options, _.acl, _.storage_class, _.request_payer)?;
        _guard_path.disarm();
        const _guard_blob = DetachGuard<@TypeOf(blob)>{ target: unsafe { &blob } };
        return S3File.S3BlobStatTask.size(globalThis, &blob);
    }

    pub fn stat(*_, globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): JSError!JSValue {
        const arguments = callframe.arguments_old(2).slice();
        let args = jsc.CallFrame.ArgumentsSlice.init(globalThis.bunVM(), arguments);
        const _guard_args = DeinitGuard<@TypeOf(args)>{ target: unsafe { &args } };
        let path = (jsc.Node.PathLike.fromJS(globalThis, &args)?) orelse {
            if (args.len() == 0) {
                return globalThis.ERR(.MISSING_ARGS, "Expected a path to check the stat of", .{}).throw();
            }
            return globalThis.throwInvalidArguments("Expected a path to check the stat of", .{});
        };
        let _guard_path = ErrDeinitGuard<@TypeOf(path)>{ target: unsafe { &path } };
        const options = args.nextEat();
        let blob = S3File.constructS3FileWithS3CredentialsAndOptions(globalThis, path, options, _.credentials, _.options, _.acl, _.storage_class, _.request_payer)?;
        _guard_path.disarm();
        const _guard_blob = DetachGuard<@TypeOf(blob)>{ target: unsafe { &blob } };
        return S3File.S3BlobStatTask.stat(globalThis, &blob);
    }

    pub fn write(*_, globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): JSError!JSValue {
        const arguments = callframe.arguments_old(3).slice();
        let args = jsc.CallFrame.ArgumentsSlice.init(globalThis.bunVM(), arguments);
        const _guard_args = DeinitGuard<@TypeOf(args)>{ target: unsafe { &args } };
        let path = (jsc.Node.PathLike.fromJS(globalThis, &args)?) orelse {
            return globalThis.ERR(.MISSING_ARGS, "Expected a path to write to", .{}).throw();
        };
        let _guard_path = ErrDeinitGuard<@TypeOf(path)>{ target: unsafe { &path } };
        const data = args.nextEat() orelse {
            return globalThis.ERR(.MISSING_ARGS, "Expected a Blob-y thing to write", .{}).throw();
        };

        const options = args.nextEat();
        let blob = S3File.constructS3FileWithS3CredentialsAndOptions(globalThis, path, options, _.credentials, _.options, _.acl, _.storage_class, _.request_payer)?;
        _guard_path.disarm();
        const _guard_blob = DetachGuard<@TypeOf(blob)>{ target: unsafe { &blob } };
        let blob_internal: PathOrBlob = .{ .blob = blob };
        return Blob.writeFileInternal(globalThis, &blob_internal, data, .{
            .mkdirp_if_not_exists = false,
            .extra_options = options,
        });
    }

    pub fn listObjects(*_, globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): JSError!JSValue {
        const args = callframe.argumentsAsArray(2);
        const object_keys = args[0];
        const options = args[1];

        let blob = S3File.constructS3FileWithS3CredentialsAndOptions(globalThis, .{ .string = PathString.empty }, options, _.credentials, _.options, null, null, _.request_payer)?;

        const _guard_blob = DetachGuard<@TypeOf(blob)>{ target: unsafe { &blob } };
        return blob.store.?.data.s3.listObjects(blob.store.?, globalThis, object_keys, options);
    }

    pub fn unlink(*_, globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): JSError!JSValue {
        const arguments = callframe.arguments_old(2).slice();
        let args = jsc.CallFrame.ArgumentsSlice.init(globalThis.bunVM(), arguments);
        const _guard_args = DeinitGuard<@TypeOf(args)>{ target: unsafe { &args } };
        let path = (jsc.Node.PathLike.fromJS(globalThis, &args)?) orelse {
            return globalThis.ERR(.MISSING_ARGS, "Expected a path to unlink", .{}).throw();
        };
        let _guard_path = ErrDeinitGuard<@TypeOf(path)>{ target: unsafe { &path } };
        const options = args.nextEat();
        let blob = S3File.constructS3FileWithS3CredentialsAndOptions(globalThis, path, options, _.credentials, _.options, _.acl, _.storage_class, _.request_payer)?;
        _guard_path.disarm();
        const _guard_blob = DetachGuard<@TypeOf(blob)>{ target: unsafe { &blob } };
        return blob.store.?.data.s3.unlink(blob.store.?, globalThis, options);
    }

    pub fn deinit(*_) {
        _.credentials.deref();
        destroy(_);
    }

    pub fn finalize(*_) {
        _.deinit();
    }

    // Static methods
    pub static fn staticWrite(globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): JSError!JSValue {
        return S3File.write(globalThis, callframe);
    }

    pub static fn staticPresign(globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): JSError!JSValue {
        return S3File.presign(globalThis, callframe);
    }

    pub static fn staticExists(globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): JSError!JSValue {
        return S3File.exists(globalThis, callframe);
    }

    pub static fn staticSize(globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): JSError!JSValue {
        return S3File.size(globalThis, callframe);
    }

    pub static fn staticUnlink(globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): JSError!JSValue {
        return S3File.unlink(globalThis, callframe);
    }

    pub static fn staticFile(globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): JSError!JSValue {
        const arguments = callframe.arguments_old(2).slice();
        let args = jsc.CallFrame.ArgumentsSlice.init(globalThis.bunVM(), arguments);
        const _guard_args = DeinitGuard<@TypeOf(args)>{ target: unsafe { &args } };

        const path = (jsc.Node.PathLike.fromJS(globalThis, &args)?) orelse {
            return globalThis.throwInvalidArguments("Expected file path string", .{});
        };

        return S3File.constructInternalJS(globalThis, path, args.nextEat())?;
    }

    pub static fn staticStat(globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): JSError!JSValue {
        return S3File.stat(globalThis, callframe);
    }

    pub static fn staticListObjects(globalThis: *JSGlobalObject, callframe: *jsc.CallFrame): JSError!JSValue {
        const args = callframe.argumentsAsArray(2);
        const object_keys = args[0];
        const options = args[1];

        const existing_credentials = globalThis.bunVM().transpiler.env.getS3Credentials();

        let blob = S3File.constructS3FileWithS3Credentials(globalThis, .{ .string = PathString.empty }, options, existing_credentials)?;

        const _guard_blob = DetachGuard<@TypeOf(blob)>{ target: unsafe { &blob } };
        return blob.store.?.data.s3.listObjects(blob.store.?, globalThis, object_keys, options);
    }
}

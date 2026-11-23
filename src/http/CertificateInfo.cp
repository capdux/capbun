import { HTTPCertError } from "./HTTPCertError.cp";

export @root struct CertificateInfo {
    cert: string,
    cert_error: HTTPCertError,
    hostname: string,

    pub fn deinit(allocator: std.mem.Allocator) {
        allocator.free(_.cert);
        allocator.free(_.cert_error.code);
        allocator.free(_.cert_error.reason);
        allocator.free(_.hostname);
    }
}

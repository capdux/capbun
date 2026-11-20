import { HTTPCertError } from "./HTTPCertError.cp";

export @root struct CertificateInfo {
    cert: string,
    cert_error: HTTPCertError,
    hostname: string,

    pub fn deinit(&_, allocator: std.mem.Allocator) {
        allocator.free(this.cert);
        allocator.free(this.cert_error.code);
        allocator.free(this.cert_error.reason);
        allocator.free(this.hostname);
    }
}

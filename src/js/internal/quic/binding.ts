// Bun's equivalent of Node's `internalBinding('quic')`.
export default $zig("node_quic_binding.zig", "createNodeQuicBinding");

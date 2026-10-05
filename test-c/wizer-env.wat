;; Build-time stand-in for the stack/TLS intrinsics that the component glue provides at runtime.
(module
  ;; Matches wasm-ld's __init_stack_pointer (default 64KiB stack placed first).
  (global $sp (mut i32) (i32.const 65536))
  (global $tls (mut i32) (i32.const 0))
  (func (export "__wasm_get_stack_pointer") (result i32) global.get $sp)
  (func (export "__wasm_set_stack_pointer") (param i32) local.get 0 global.set $sp)
  (func (export "__wasm_get_tls_base") (result i32) global.get $tls)
  (func (export "__wasm_set_tls_base") (param i32) local.get 0 global.set $tls))


import os
import subprocess

def verify_all_cp_files(directory):
    for root, dirs, files in os.walk(directory):
        for file in files:
            if file.endswith('.cp'):
                cp_path = os.path.join(root, file)
                zig_path = os.path.join('/data/dev/bun/temp/', file.replace('.cp', '.zig'))
                
                print(f"Verifying {cp_path}...")
                
                cpc_res = subprocess.run(['/data/dev/cp/cpc/target/debug/cpc', '--lib', cp_path, '-o', zig_path], capture_output=True, text=True)
                if cpc_res.returncode != 0:
                    print(f"CPC failed for {cp_path}: {cpc_res.stderr}")
                    continue
                    
                ast_res = subprocess.run(['/data/dev/bun/vendor/zig/zig', 'ast-check', zig_path], capture_output=True, text=True)
                if ast_res.returncode != 0:
                    print(f"AST-check failed for {cp_path}: {ast_res.stderr}")
                    continue
                
                print(f"Verified {cp_path}")

verify_all_cp_files('/data/dev/bun/src/')

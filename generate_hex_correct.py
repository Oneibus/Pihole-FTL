#!/usr/bin/env python3
"""Generate C header files matching CMake xxd output format."""

import os
import sys

def generate_hex_file(input_file, output_file, variable_name):
    """Convert a file to a C-style hex array matching xxd -iC format."""
    with open(input_file, 'rb') as f:
        content = f.read()
    
    hex_lines = []
    for i in range(0, len(content), 12):
        chunk = content[i:i+12]
        hex_bytes = ', '.join(f'0x{b:02x}' for b in chunk)
        hex_lines.append(f'  {hex_bytes}')
    
    hex_string = ',\n'.join(hex_lines).rstrip(', ')
    
    header = f"""static const unsigned char {variable_name}[] = {{
{hex_string}
}};
static const unsigned int {variable_name}_len = {len(content)};
"""
    
    with open(output_file, 'w') as f:
        f.write(header)
    
    print(f"Generated {output_file} ({len(content)} bytes)")

def main():
    docs_dir = '/mnt/e/Projects/source/Projects/PiHole/FTL/src/api/docs'
    content_dir = os.path.join(docs_dir, 'content')
    hex_dir = os.path.join(docs_dir, 'hex')
    
    os.makedirs(os.path.join(hex_dir, 'specs'), exist_ok=True)
    os.makedirs(os.path.join(hex_dir, 'images'), exist_ok=True)
    os.makedirs(os.path.join(hex_dir, 'external'), exist_ok=True)
    
    file_mappings = [
        ('index.html', 'index_html.h', 'index_html'),
        ('index.css', 'index_css.h', 'index_css'),
        ('pi-hole.js', 'pi_hole_js.h', 'pi_hole_js'),
        ('external/rapidoc-min.js', 'external/rapidoc_min_js.h', 'external_rapidoc_min_js'),
        ('external/rapidoc-min.js.map', 'external/rapidoc_min_js_map.h', 'external_rapidoc_min_js_map'),
        ('external/highlight-default.min.css', 'external/highlight_default_min_css.h', 'external_highlight_default_min_css'),
        ('external/highlight.min.js', 'external/highlight_min_js.h', 'external_highlight_min_js'),
        ('images/logo.svg', 'images/logo_svg.h', 'images_logo_svg'),
        ('images/favicon.ico', 'images/favicon_ico.h', 'images_favicon_ico'),
        ('specs/action.yaml', 'specs/action_yaml.h', 'specs_action_yaml'),
        ('specs/auth.yaml', 'specs/auth_yaml.h', 'specs_auth_yaml'),
        ('specs/clients.yaml', 'specs/clients_yaml.h', 'specs_clients_yaml'),
        ('specs/config.yaml', 'specs/config_yaml.h', 'specs_config_yaml'),
        ('specs/common.yaml', 'specs/common_yaml.h', 'specs_common_yaml'),
        ('specs/dhcp.yaml', 'specs/dhcp_yaml.h', 'specs_dhcp_yaml'),
        ('specs/dns.yaml', 'specs/dns_yaml.h', 'specs_dns_yaml'),
        ('specs/docs.yaml', 'specs/docs_yaml.h', 'specs_docs_yaml'),
        ('specs/domains.yaml', 'specs/domains_yaml.h', 'specs_domains_yaml'),
        ('specs/endpoints.yaml', 'specs/endpoints_yaml.h', 'specs_endpoints_yaml'),
        ('specs/groups.yaml', 'specs/groups_yaml.h', 'specs_groups_yaml'),
        ('specs/history.yaml', 'specs/history_yaml.h', 'specs_history_yaml'),
        ('specs/info.yaml', 'specs/info_yaml.h', 'specs_info_yaml'),
        ('specs/lists.yaml', 'specs/lists_yaml.h', 'specs_lists_yaml'),
        ('specs/logs.yaml', 'specs/logs_yaml.h', 'specs_logs_yaml'),
        ('specs/main.yaml', 'specs/main_yaml.h', 'specs_main_yaml'),
        ('specs/network.yaml', 'specs/network_yaml.h', 'specs_network_yaml'),
        ('specs/padd.yaml', 'specs/padd_yaml.h', 'specs_padd_yaml'),
        ('specs/queries.yaml', 'specs/queries_yaml.h', 'specs_queries_yaml'),
        ('specs/search.yaml', 'specs/search_yaml.h', 'specs_search_yaml'),
        ('specs/stats.yaml', 'specs/stats_yaml.h', 'specs_stats_yaml'),
        ('specs/teleporter.yaml', 'specs/teleporter_yaml.h', 'specs_teleporter_yaml'),
    ]
    
    success_count = 0
    for source_file, hex_file, var_name in file_mappings:
        input_path = os.path.join(content_dir, source_file)
        output_path = os.path.join(hex_dir, hex_file)
        
        if os.path.exists(input_path):
            try:
                generate_hex_file(input_path, output_path, var_name)
                success_count += 1
            except Exception as e:
                print(f"Error processing {source_file}: {e}")
    
    print(f"\nGenerated {success_count}/{len(file_mappings)} files")

if __name__ == '__main__':
    main()

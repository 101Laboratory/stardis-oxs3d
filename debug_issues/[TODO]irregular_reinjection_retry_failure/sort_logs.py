import re
import sys

def sort_logs(input_file, output_file=None):
    if output_file is None:
        output_file = input_file.rsplit('.', 1)[0] + '_sorted.txt'

    with open(input_file, 'r') as f:
        lines = [line.rstrip('\n') for line in f if line.strip()]

    def sort_key(line):
        path_match = re.search(r'path=(\d+)', line)
        retry_match = re.search(r'retry=(\d+)', line)
        path_id = int(path_match.group(1)) if path_match else 0
        retry = int(retry_match.group(1)) if retry_match else 0
        return (int(path_id), int(retry))

    lines.sort(key=sort_key)

    with open(output_file, 'w') as f:
        for line in lines:
            f.write(line + '\n')

    print(f"Sorted {len(lines)} lines -> {output_file}")

if __name__ == '__main__':
    input_file = sys.argv[1] if len(sys.argv) > 1 else 'logs.txt'
    output_file = sys.argv[2] if len(sys.argv) > 2 else None
    sort_logs(input_file, output_file)

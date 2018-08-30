#!/usr/bin/env python
import subprocess
import itertools

def main():
  yes_no = True, False
  buffer_sizes = ['1024', '4096', '8192', '16384']

  subprocess.check_call(['ninja', '-C', 'out/android_debug_arm'])
  subprocess.check_call(['adb', 'push', 'out/android_debug_arm/ftrace_tester', '/data/misc/ftrace_tester'])



  print '\t'.join(['clear', 'clear_cpus', 'buffer_size', 'set_buffer_size'])
  for args in itertools.product(yes_no, yes_no, buffer_sizes, yes_no):
    should_clear, should_clear_cpus, buffer_size, should_change_buffer_size = args
    cmdline = []
    if should_clear:
      cmdline.append('--clear')
    if should_clear_cpus:
      cmdline.append('--clear-cpus')
    if buffer_size:
      cmdline.append('--buffer')
      cmdline.append(buffer_size)
    if should_change_buffer_size:
      cmdline.append('--set-buffer')
    output = subprocess.check_output(['adb', 'shell', '/data/misc/ftrace_tester'] + cmdline)
    #print cmdline
    #print output
    print '\t'.join([
        str(should_clear),
        str(should_clear_cpus),
        buffer_size,
        str(should_change_buffer_size),
    ] + filter(None, output.split('ms\n')))
if __name__ == '__main__':
  main()

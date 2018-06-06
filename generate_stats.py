# Copyright (C) 2018 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from __future__ import division

import json

from matplotlib import pyplot as plt
import numpy as np
import seaborn as sns
import scipy as sp


with open('heapinfo') as fd:
  j = json.load(fd)


# from matplotlib.backends.backend_pdf import PdfPages
# pp = PdfPages('foo.pdf')

allocs = np.array(j['alloc_histogram'])
allocs.sort()
send = np.array(j['send_histogram'])
send.sort()
unwind_only = np.array(j['unwind_only_histogram'])
unwind_only.sort()
stack = np.array(j['stack_histogram'])
stack.sort()
gap = np.array(j['gap_histogram'])
gap.sort()
unwind_diff = np.array(j['unwind_diff_histogram'])
unwind_diff.sort()

sns.set()

def save(name, fig):
  plt.savefig(name)
  # pp.savefig(fig)

fig = plt.figure()
plt.title("Allocations between alloc + free of same address")
plt.xlabel("Allocations")
plt.ylabel("Fraction of samples")
plt.plot(allocs, np.arange(1, len(allocs) + 1) / float(j['samples_handled'] + j['samples_failed']))
save("img/alloc.png", fig)

fig = plt.figure()
plt.title("Stack sizes (bytes)")
plt.xlabel("Stack size")
plt.ylabel("Fraction of samples")
plt.plot(stack, np.linspace(0, 1, len(stack), len(stack)))
save("img/stack_cdf.png", fig)
fig = plt.figure()
plt.title("Stack sizes (bytes)")
sns.distplot(stack)
save("img/stack_dist.png", fig)
fig = plt.figure()
plt.title("Stack sizes (bytes)")
save("img/stack_bplot.png", fig)

fig = plt.figure()
plt.title("Send / Unwind / Gap CDF (us clipped to 4000)")
plt.plot(gap, np.linspace(0, 1.0, len(gap)), label="Gap")
plt.plot(send, np.linspace(0, 1.0, len(send)), label="Send")
plt.plot(unwind_only, np.linspace(0, 1.0, len(unwind_only)), label="Unwind")
plt.xlabel("time (us)")
plt.ylabel("fraction of samples")
plt.legend()
plt.xlim(0, 4000)
save("img/timing_cdf.png", fig)

fig = plt.figure()
plt.title("Send / Unwind CDF (us clipped to 4000)")
plt.plot(send, np.linspace(0, 1.0, len(send)), label="Send")
plt.plot(unwind_only, np.linspace(0, 1.0, len(unwind_only)), label="Unwind")
plt.xlabel("time (us)")
plt.ylabel("fraction of samples")
plt.legend()
plt.xlim(0, 4000)
save("img/timing_nogap_cdf.png", fig)


fig = plt.figure()
plt.title("Send / Unwind / Gap")
ax = plt.gca()
plt.boxplot([unwind_only, send, gap], showfliers=False)
ax.set_xticklabels(['Unwind', 'Send', 'Gap'])
# sns.boxplot(unwind_only)
save("img/timing_bplot.png", fig)

fig = plt.figure()
plt.title("Send / Unwind (no outliers)")
ax = plt.gca()
plt.boxplot([unwind_only, send], showfliers=False)
ax.set_xticklabels(['Unwind', 'Send'])
plt.ylabel('time (us)')
# sns.boxplot(unwind_only)
save("img/timing_nogap_bplot.png", fig)

fig = plt.figure()
plt.title("Unwind diff")
sns.boxplot(unwind_diff, showfliers=False)
save("img/unwind_diff_bplot.png", fig)

fig = plt.figure()
plt.title("Unwind diff")
plt.plot(unwind_diff, np.linspace(0, 1.0, len(unwind_diff)), label="Unwind diff")
save("img/unwind_diff_dist.png", fig)

# pp.close()

print "Alloc Summary"
print sp.stats.describe(allocs)
print "Send Summary"
print sp.stats.describe(send)
print "Unwind Summary"
print sp.stats.describe(unwind_only)
print "Gap Summary"
print sp.stats.describe(gap)
print "Stack Summary"
print sp.stats.describe(stack)
print "Unwind diff Summary"
print sp.stats.describe(unwind_diff[unwind_diff > -1000000])

print "Allocs 20pct", np.percentile(allocs, 20)
print "Send 50pct", np.percentile(send, 50)
print "Unwind 50pct", np.percentile(unwind_only, 50)
print "Send 90pct", np.percentile(send, 90)
print "Unwind 90pct", np.percentile(unwind_only, 90)

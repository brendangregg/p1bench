p1bench
=======

Perturbation benchmark.

This is intended to be used before other benchmark tests, to investigate CPU and memory variation. This helps you better interpret the results of any microbenchmark test, by characterizing variance that will sway results.

Let's say you wanted to do a 500 ms CPU benchmark of gzip performance: p1bench can be run with a 500 ms interval to show what baseline variation you may see, based on a simple spin loop, before running a more complex gzip microbenchmark.

p1bench can also be run in a mode (-m) to test memory variation.

## Operating Systems

Tested on Linux and OSX. Should work anywhere with a C compiler and libpthread.

## Compile

```
gcc -O0 -pthread -o p1bench p1bench.c
```

## Screenshots

CPU test for a 500 ms duration:

<pre>
$ <b>./p1bench 500</b>
Calibrating for 500 ms... (target iteration count: 220661127)
Run 100/100, Ctrl-C to stop (-0.30% diff)

Perturbation percent by count for 500 ms runs:
  Slower%   Count  Count% Histogram
     0.0%:      5   5.00% *********
     0.1%:      5   5.00% *********
     0.2%:      2   2.00% ****
     0.3%:      2   2.00% ****
     0.4%:     11  11.00% *******************
     0.5%:      4   4.00% *******
     0.6%:      3   3.00% *****
     0.7%:      4   4.00% *******
     0.8%:      5   5.00% *********
     0.9%:      1   1.00% **
     1.0%:     30  30.00% **************************************************
     2.0%:     27  27.00% *********************************************
     3.0%:      1   1.00% **

Percentiles: 50th: 1.356%, 90th: 2.439%, 99th: 2.954%, 100th: 3.035%
Fastest: 500.715 ms, 50th: 507.504 ms, mean: 507.359 ms, slowest: 515.911 ms
Fastest rate: 440692064/s, 50th: 434796823/s, mean: 434921085/s, slowest: 427711614/s
</pre>

Many numbers are printed to characterize variance, and the histogram shows it visually. Just from the histogram, I'd expect a variance of up to 2% (fastest to slowest) for a CPU microbenchmark of the same duration (500 ms).

This is a custom histogram, where the bin size varies:

- variation 0 - 1%: 0.1% binsize
- variation 1 - 20%: 1% binsize
- variation 20+%: 10% binsize

Here's a much noisier system:

<pre>
$ <b>./p1bench 500</b>
Calibrating for 500 ms... (target iteration count: 206071738)
Run 100/100, Ctrl-C to stop (0.85% diff)

Perturbation percent by count for 500 ms runs:
  Slower%   Count  Count% Histogram
     0.0%:      1   1.00% ****
     0.1%:      0   0.00%
     0.2%:      0   0.00%
     0.3%:      0   0.00%
     0.4%:      1   1.00% ****
     0.5%:      0   0.00%
     0.6%:      0   0.00%
     0.7%:      0   0.00%
     0.8%:      1   1.00% ****
     0.9%:      0   0.00%
     1.0%:      1   1.00% ****
     2.0%:     10  10.00% ********************************
     3.0%:      7   7.00% **********************
     4.0%:     12  12.00% **************************************
     5.0%:     14  14.00% ********************************************
     6.0%:     10  10.00% ********************************
     7.0%:     16  16.00% **************************************************
     8.0%:      5   5.00% ****************
     9.0%:     10  10.00% ********************************
    10.0%:      5   5.00% ****************
    11.0%:      1   1.00% ****
    12.0%:      2   2.00% *******
    13.0%:      1   1.00% ****
    14.0%:      1   1.00% ****
    15.0%:      1   1.00% ****
    16.0%:      0   0.00%
    17.0%:      0   0.00%
    18.0%:      0   0.00%
    19.0%:      0   0.00%
    20.0%:      0   0.00%
    30.0%:      1   1.00% ****

Percentiles: 50th: 6.258%, 90th: 10.085%, 99th: 15.431%, 100th: 38.078%
Fastest: 485.364 ms, 50th: 515.739 ms, mean: 518.336 ms, slowest: 670.182 ms
Fastest rate: 424571533/s, 50th: 399565939/s, mean: 397564008/s, slowest: 307486232/s
</pre>

If I wanted to do a 500 ms CPU microbenchmark on this system, well, I'd find another system.

USAGE:

<pre>
USAGE: p1bench [-hv] [-m Mbytes] [time(ms) [count]]
                   -v         # verbose: per run details
                   -m Mbytes  # memory test working set
   eg,
       p1bench          # 100ms (default) CPU spin loop
       p1bench 300      # 300ms CPU spin loop
       p1bench 300 100  # 300ms CPU spin loop, 100 times
       p1bench -m 1024  # 1GB memory read loop
</pre>

# vpdf-sync
A tool for synchronizing screen-cast recordings to presentation slides.
Results are an injective mapping from frame indices/timestamps to page numbers.

## Building
On a system equipped with the appropriate libraries and respective header files,
a simple

	$ make

suffices. Optional libraries include poppler-glib, poppler-cpp, ghostscript, lzo,
zlib and OpenMP. The Makefile has a documentation on the features those are used
for. Dependencies to these libraries may be disabled there if desired. The FFmpeg
library is a hard requirement as it is used to decode video frames.

## How synchronization works
Basically, the algorithm first renders the slides to appropriate image size and
then decodes the supplied video file, checking each frame for a closest match to
the slides. It then outputs consecutive runs of found slide numbers.

### Comparisons
vpdf-sync is based on comparisons of rendered slides to video frames. The metric
employed for this is SSIM (Structural SIMilarity), an block-based index that's
widely used for image reproduction quality assessments. It's a formula based on
averages, variances and covariances of pixels in an 8x8 pixel block.

A sample output looks like this:

	00:00:00.02 - 00:00:12.92 frames     0 to   129 show page  859 w/ ssim 0.9878 to 0.9880
	00:00:13.02 - 00:02:02.02 frames   130 to  1220 show page  860 w/ ssim 0.9832 to 0.9846
	00:02:02.12 - 00:03:17.62 frames  1221 to  1976 show page  861 w/ ssim 0.9839 to 0.9853
	00:03:17.72 - 00:03:55.22 frames  1977 to  2352 show page  862 w/ ssim 0.9838 to 0.9848
	00:03:55.32 - 00:04:23.72 frames  2353 to  2637 show page  863 w/ ssim 0.9836 to 0.9847
	00:04:23.82 - 00:04:35.82 frames  2638 to  2758 show page  864 w/ ssim 0.9841 to 0.9851
	00:04:35.92 - 00:05:26.82 frames  2759 to  3268 show page  865 w/ ssim 0.9843 to 0.9853

### Difficulties
The currently only supported slide format is PDF. Due to multiple possible
renderers for PDF (poppler, ghostscript, Acrobat, etc.) the resulting pictures
don't always look the same. In fact, each renderer further supports a multitude
of "output devices", which also influence the final rendering (sometimes on a
subpixel level within the same library, e.g. the cairo (glib) and splash (cpp)
backends to poppler).

Additionally, viewers like Okular (from KDE), evince (Gnome) and the like don't
just enable use of different rendering backends (as vpdf-sync does), but also
seem to not always adhere to the aspect ratio of the slides. E.g. Okular
randomly full-screen renders 4:3 slides to e.g. 1021x768 pixels on a 1024x768
monitor. This cannot be detected by vpdf-sync thus far and has to be supplied
by the user via option '-C' if necessary. To make this task easier, options
'-D' and '-V' provide access to the renderings and frames used for comparisons
which allow a human to easily observe the required cropping.

### Implementational details
The algorithm works in 4 steps:

1. find out the dimension to which to render the slides to from the video
   and render all slides to that dimension to memory (optionally compressed,
   see Makefile)

2. for each slide compute a threshold below which a video frame must be
   considered to differ from the last (and can therefore not use the previous
   slide index result)

3. decode the video, check each frame against it's predecessor and if they
   differ too much, compare it to all slides; take the index of the slide
   which matches most closely according to the metric used

4. output ranges of video frame indices matching one slide in conjunction
   with ranges of corresponding similarity values ("certainty")

Steps 1, 2 and 3 are parallelized (if OpenMP is used) and typically take a
minute or two on recent hardware. Main memory consumption is about 80 MiB
for 100 slides @1024x768 and 200 MiB for 1000 slides (lzo-compressed) (160
MiB or 1.2 GiB respectively for uncompressed memory storage).

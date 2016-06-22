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

	00:00:00.02 - 00:00:38.52 frames     0 to   385 show page  619 (78) w/ ssim 0.9327 to 0.9758 exact
	00:00:38.62 - 00:01:18.72 frames   386 to   787 show page  620 (79) w/ ssim 0.9691 to 0.9710 exact
	00:01:18.82 - 00:01:36.92 frames   788 to   969 show page  621 (79) w/ ssim 0.9684 to 0.9691 exact
	00:01:37.02 - 00:01:37.52 frames   970 to   975 show page  757 (92) w/ ssim 0.1524 to 0.1604 vague
	00:01:37.62 - 00:01:38.92 frames   976 to   989 show page  689 (86) w/ ssim 0.1693 to 0.1831 vague
	00:01:39.02 - 00:02:00.72 frames   990 to  1207 show page  757 (92) w/ ssim 0.1607 to 0.2529 vague
	00:02:00.82 - 00:02:03.02 frames  1208 to  1230 show page  621 (79) w/ ssim 0.9682 to 0.9691 exact
	00:02:03.12 - 00:02:16.52 frames  1231 to  1365 show page  622 (79) w/ ssim 0.9677 to 0.9678 exact

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
monitor. This can be detected by vpdf-sync via option '-C detect'. If cropping
is necessary and the region is known it can be supplied by the user via option
'-C' to save computing an additional run over all slides and video frames.

Options '-D' and '-V' provide access to the renderings and frames used for the
comparisons which allow to be used as previews in an interactive video player
and to also let a human easily observe required cropping.

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

## Usage info
```
usage: ./vpdf-sync [-OPTS] [--] VID REN_OPTS...

  VID          path to screen-cast video file
  REN_OPTS...  options to slide renderer, see options '-R' and '-r' for details

Options [defaults]:
  -c X:Y       set cut-off thresholds of abs-diff luma components of averaged
               slides and frames for crop-detection, either can be empty [32:32]
  -C T:B:L:R   pad pixels to renderings wrt. VID; comp < 0 to detect [0:0:0:0]
  -C detect    detect the cropping area based on the average intensity of slides
  -d VID_DIFF  interpret consecutive frames as equal if SSIM >= VID_DIFF [unset]
               (overrides -e)
  -D DIR       dump rendered images into DIR (named PAGE-REN.ppm.gz)
  -e RDIFF_TH  interpret consecutive frames as equal if SSIM >= RDIFF + TH where
               RDIFF is computed as max SSIM from this to another rendered
               frame and TH = (1-RDIFF)*RDIFF_TH, i.e. RDIFF_TH is the max.
               expected decrease of turbulence of VID frames wrt. RDIFF till
               which they're still not regarded as equal [0.125]
  -h           display this help message
  -j           format output as JSON records [human readable single line]
  -L           display list of compiled/linked libraries
  -p FROM:TO   interval of pages to render (1-based, inclusive, each),
               FROM and TO can both be empty [1:page-num]
  -r REN       use REN to render PDF [poppler-cairo]
  -R           display usage information for all supported renderers
  -u           don't compress pages (watch out for OOM) [LZO-compress]
  -v           increase verbosity
  -V DIR       dump located frames into DIR (named PAGE-FRAME-SSIM.ppm.gz)
  -y           toggle compare luma plane only [YUV]

Classification of match certainty:
  'exact' when SSIM >= 0.95 (pretty much sure),
  'vague' when SSIM <  0.4  (most probably no match found in page range),
  'fuzzy' otherwise         (match unclear, try adjusting '-C')

Author: Franz Brausse <dev@karlchenofhell.org>; vpdf-sync licensed under AGPLv3.
```

```
Renderer 'poppler-cairo' [can render: yes] usage: [-k PASSWD] [--] PDF-PATH
  -k PASSWD    use PASSWD to open protected PDF [(unset)]

Renderer 'poppler-splash' [can render: yes] usage: [-k PASSWD] [-astT] [--] PDF-PATH
  -a           disable graphics anti-aliasing
  -k PASSWD    use PASSWD to open protected PDF [(unset)]
  -s           disable preservation of aspect ratio
  -t           disable text anti-aliasing
  -T           disable text hinting

Renderer 'ghostscript' [can render: yes] usage: [--] PDF-PATH
```

```
Libraries   (compiled,	linked):
  zlib      (1.2.8,	1.2.8)
  LZO       (2.08,	2.08)
  OpenMP    (201307)
  avformat  (56.40.101,	56.40.101)
  avcodec   (56.60.100,	56.60.100)
  avutil    (54.31.100,	54.31.100)
  swscale   (3.1.101,	3.1.101)
```

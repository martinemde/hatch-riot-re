"""sd_cache: a media source that caches web audio on the Hatch's SD card.

Configured as a `media_source` platform (see media_source.py). Listed before `audio_http` in a media pipeline, it
takes every http(s) URL. With caching enabled and the card mounted, a cached URL plays from the card and anything
else streams from the web and is written to the card as it plays. Disabled (or with no card), it only streams.
Either way it sends mono as stereo (16-bit mono at 44.1 kHz plays at half speed on this I2S speaker) and, when the
download is slower than the audio, buffers enough before playing that it doesn't stutter.

The card is the stock one on SDMMC slot 1, 1-bit (CLK GPIO14, CMD GPIO15, D0 GPIO2), powered by expander P1_1. Only
files inside the cache directory are ever created or deleted; the stock content is left alone.
"""

CODEOWNERS = ["@w1ll1am23"]

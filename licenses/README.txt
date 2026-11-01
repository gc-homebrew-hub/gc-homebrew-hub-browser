Third-party software in GC Homebrew Hub Browser
===============================================

GC Homebrew Hub Browser itself is under the MIT License (license.txt next to
boot.dol, LICENSE in the source). boot.dol also contains, or is shipped with,
the following; each file in this folder is that project's licence.

Built into boot.dol
-------------------
Mbed TLS 3.6.2        Apache-2.0 (dual-licensed Apache-2.0 OR GPL-2.0-or-later;
                      used here under Apache-2.0)            mbedtls.txt
                      https://github.com/Mbed-TLS/mbedtls
libogc2               libogc licence (zlib-style)            libogc2.txt
                      parts from RTEMS: BSD-2-Clause         libogc2-rtems.txt
                      https://github.com/extremscorner/libogc2
lwIP (libbba, part    BSD-3-Clause                           lwip.txt
  of libogc2)         https://savannah.nongnu.org/projects/lwip/
libfat                BSD-3-Clause                           libfat.txt
                      https://github.com/extremscorner/libfat
zlib                  zlib License                           zlib.txt
                      https://zlib.net
liblzma (XZ Utils)    0BSD (public domain before 5.6)        liblzma.txt
                      https://tukaani.org/xz/
Inter (the text)      SIL Open Font License 1.1              inter-ofl.txt
                      (tools/font/OFL.txt in the source)
                      https://rsms.me/inter/

Shipped next to boot.dol
------------------------
cacert.pem            CA certificates from Mozilla, via curl's
                      bundle; Mozilla Public License 2.0     mpl-2.0.txt
                      https://curl.se/docs/caextract.html

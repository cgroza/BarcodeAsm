bin_PROGRAMS = BarcodeAsm

BarcodeAsm_CPPFLAGS = \
	-I$(top_srcdir)/CTPL \
	-I$(top_srcdir)/SeqLib \
	-I$(top_srcdir)/SeqLib/htslib -Wno-sign-compare

BarcodeAsm_LDADD = \
	$(top_builddir)/minimap2/libminimap2.a \
	$(top_builddir)/SeqLib/src/libseqlib.a \
	$(top_builddir)/SeqLib/bwa/libbwa.a \
	$(top_builddir)/SeqLib/htslib/libhts.a \
	$(top_builddir)/SeqLib/fermi-lite/libfml.a \
	-llzma -lbz2 -lz

BarcodeAsm_SOURCES = BarcodeAsm.cpp BxBamWalker.cpp RegionFileReader.cpp LocalAssemblyWindow.cpp LocalAlignment.cpp ContigAlignment.cpp

install:
	mkdir -p ../../bin && mv BarcodeAsm ../../bin

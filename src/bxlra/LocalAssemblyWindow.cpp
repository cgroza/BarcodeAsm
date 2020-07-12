#include "LocalAssemblyWindow.h"
#include "ASQG.h"
#include "BxBamWalker.h"
#include "OverlapAlgorithm.h"
#include "RLBWT.h"
#include "SGUtil.h"
#include "SeqLib/SeqLibUtils.h"
#include "SeqLib/UnalignedSequence.h"
#include "SuffixArray.h"
#include "Util.h"
#include <sstream>
#include <string>

LocalAssemblyWindow::LocalAssemblyWindow(SeqLib::GenomicRegion region,
                                         SeqLib::BamReader &bam,
                                         BxBamWalker &bx_bam)
    : m_region(region), m_bam(bam), m_bx_bam(bx_bam) {

  std::stringstream prefix_ss;
  prefix_ss << region.ChrName(bam.Header()) << region.pos1 << "_" << region.pos2
            << "_";
  m_prefix = prefix_ss.str();
}

size_t LocalAssemblyWindow::retrieveGenomewideReads() {
  BxBarcodeCounts barcodes = collectLocalBarcodes();

  // Barcode frequency in assembly window
  std::cerr << std::endl;
  for (const auto &b : barcodes) {
    std::cerr << b.first << " " << b.second << std::endl;
  }
  std::cerr << std::endl;

  m_reads = m_bx_bam.fetchReadsByBxBarcode(barcodes);
  return m_reads.size();
}

size_t LocalAssemblyWindow::assembleReads() {
  // TODO: Add inexact read overlap by learning read parameters
  retrieveGenomewideReads();
  createReadTable();
  // forward suffix index
  SuffixArray suffix_array_f(&m_read_table, 1, false);
  RLBWT BWT_f(&suffix_array_f, &m_read_table);

  // reverse suffix index
  m_read_table.reverseAll();
  SuffixArray suffix_array_r(&m_read_table, 1, false);
  RLBWT BWT_r(&suffix_array_r, &m_read_table);

  // reverse the read table back to its forward state
  m_read_table.reverseAll();

  suffix_array_f.writeIndex();
  suffix_array_r.writeIndex();

  // TODO: Add read deduplication. Necessary for read overlap to work.

  OverlapAlgorithm overlapper(&BWT_f, &BWT_r, m_params.error_rate,
                              m_params.seed_length, m_params.seed_stride,
                              m_params.irr_only);

  bool exact = m_params.error_rate < 0.0001;
  overlapper.setExactModeOverlap(exact);
  overlapper.setExactModeIrreducible(exact);

  // write ASQG in memory, dump later.
  std::stringstream asqg_writer;

  // Write ASGQ header
  ASQG::HeaderRecord header_record;
  header_record.setOverlapTag(m_params.min_overlap);
  header_record.setErrorRateTag(m_params.error_rate);
  header_record.setContainmentTag(true);
  header_record.setTransitiveTag(!m_params.irr_only);
  header_record.write(asqg_writer);

  // reset read table iterator
  m_read_table.setZero();

  size_t workid = 0;
  SeqItem si;

  std::stringstream hits_stream;
  // find read overlaps for each read
  while (m_read_table.getRead(si)) {
    SeqRecord read;
    read.id = si.id;
    read.seq = si.seq;
    OverlapBlockList obl;

    OverlapResult rr = overlapper.overlapRead(read, m_params.min_overlap, &obl);
    overlapper.writeOverlapBlocks(hits_stream, workid, rr.isSubstring, &obl);

    // write a vertex for this read
    ASQG::VertexRecord record(read.id, read.seq.toString());
    record.setSubstringTag(rr.isSubstring);
    record.write(asqg_writer);

    ++workid;
  }

  // parse hits and write edges for each read vertex
  std::string line;
  bool b_is_self_compare = true;
  ReadInfoTable query_read_info_table(&m_read_table);

  while (std::getline(hits_stream, line)) {
    size_t read_idx;
    size_t total_entries;
    bool is_substring;
    OverlapVector ov;
    OverlapCommon::parseHitsString(line, &query_read_info_table,
                                   &query_read_info_table, &suffix_array_f,
                                   &suffix_array_r, b_is_self_compare, read_idx,
                                   total_entries, ov, is_substring);

    for (OverlapVector::iterator iter = ov.begin(); iter != ov.end(); ++iter) {
      ASQG::EdgeRecord edge_record(*iter);
      edge_record.write(asqg_writer);
    }
  }

  // dump assembly graph to file
  std::ofstream asqg_file_writer;
  asqg_file_writer.open(m_prefix + "graph.asqg");
  asqg_file_writer << asqg_writer.str();
  asqg_file_writer.close();

  // now retrieve sequences from the graph
  assembleFromGraph(asqg_writer, exact);
  return m_contigs.size();
}

void LocalAssemblyWindow::assembleFromGraph(std::stringstream &asqg_stream,
                                            bool exact) {

  // configure string graph object
  StringGraph *str_graph =
      SGUtil::loadASQG(asqg_stream, m_params.min_overlap, true);
  str_graph->m_get_components = m_params.get_components;
  str_graph->setExactMode(exact);

  // The returned contigs are enumerations of walks in the ASGQ graph
  // The following preprocessing steps remove unwanted walks

  SGTransitiveReductionVisitor trans_visitor;
  SGGraphStatsVisitor stat_visitor;
  SGTrimVisitor trim_visitor(m_params.trim_length_threshold);
  SGContainRemoveVisitor contain_visitor;
  SGValidateStructureVisitor validate_visitor;
  SGVisitorContig contig_visitor;

  // ?????
  while (str_graph->hasContainment())
    str_graph->visit(contain_visitor);

  // removes redundant paths from the graph
  if (m_params.perform_trim)
    str_graph->visit(trans_visitor);

  str_graph->simplify(); // merges vertices that do not branch

  if (m_params.validate)
    str_graph->visit(validate_visitor);

  // Remove branches that do not merge into to form a bubble
  for (size_t i = 0; i < m_params.trim_rounds; i++)
    str_graph->visit(trim_visitor);

  // identify these vertices with a unique prefix
  str_graph->renameVertices(m_prefix);

  // walk contigs
  str_graph->visit(contig_visitor);

  // copy contigs from visitor into this object
  m_contigs = contig_visitor.m_ct;

  str_graph -> writeASQG(m_prefix + "pruned_graph.asqg");
  delete str_graph;
}

BxBarcodeCounts LocalAssemblyWindow::collectLocalBarcodes() {
  std::cerr << m_region.ToString(m_bam.Header()) << std::endl;
  m_bam.SetRegion(m_region);

  BamReadVector read_vector;

  while (true) {
    // Retrieve all reads within this region.
    SeqLib::BamRecord bam_record;

    if (m_bam.GetNextRecord(bam_record)) {
      read_vector.push_back(bam_record);
    } else
      break;
  }

  return BxBamWalker::collectBxBarcodes(read_vector);
}

SeqLib::UnalignedSequenceVector LocalAssemblyWindow::getContigs() const {
  return m_contigs;
}

BamReadVector LocalAssemblyWindow::getReads() const { return m_reads; }

void LocalAssemblyWindow::createReadTable() {
  size_t count = 0;
  for (auto &i : m_reads) {
    std::string seq = i.Sequence();
    // ignore uncalled bases or reads that are too short
    // IMPORTANT: cannot build suffix array/index when reads have uncalled bases
    if (seq.find("N") != std::string::npos ||
        seq.length() < m_params.min_overlap)
      continue;
    std::string seq_id = std::to_string(++count);

    if (!i.MappedFlag() && !i.MateReverseFlag())
      SeqLib::rcomplement(seq);

    SeqItem seq_item;
    seq_item.seq = seq;
    seq_item.id = seq_id;
    m_read_table.addRead(seq_item);
  }
}

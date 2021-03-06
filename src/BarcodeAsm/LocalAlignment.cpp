#include "LocalAlignment.h"

LocalAlignment::LocalAlignment(std::string chr, size_t start, size_t end,
                               const SeqLib::RefGenome &genome)
{
    std::string region = genome.QueryRegion(chr, start, end);
    // identifier for the target aligned region
    std::stringstream s;
    s << chr << "_" << start << "_" << end;
    m_target_name = s.str();

    setupIndex(region);
}

LocalAlignment::LocalAlignment(std::string target_sequence, std::string target_name) : m_target_name(target_name) {
  setupIndex(target_sequence);
}

void LocalAlignment::setupIndex(std::string target_sequence) {
  m_local_sequence = new char[target_sequence.size() + 1];
  memcpy(m_local_sequence, target_sequence.c_str(), target_sequence.size() + 1);

  mm_set_opt(0, &m_index_opt, &m_map_opt);
  m_map_opt.flag |= MM_F_CIGAR; // perform base level alignment

  // tune minimap for large gaps
  m_map_opt.max_join_short = m_params.max_join_short;
  m_map_opt.max_join_long = m_params.max_join_long;
  m_map_opt.min_join_flank_sc = m_params.min_join_flank_sc;
  m_map_opt.min_join_flank_ratio = m_params.min_join_flank_ratio;
  m_map_opt.max_gap = m_params.max_gap;
  m_map_opt.bw = m_params.bw;
  m_map_opt.pri_ratio = m_params.pri_ratio;
  m_map_opt.max_chain_skip = m_params.max_chain_skip;
  m_map_opt.end_bonus = m_params.end_bonus;
  m_map_opt.chain_gap_scale = m_params.chain_gap_scale;
  m_map_opt.min_chain_score = m_params.min_chain_score;
  m_map_opt.e = m_params.e;
  m_map_opt.e2 = m_params.e2;
  m_map_opt.q = m_params.q;
  m_map_opt.q2 = m_params.q2;
  m_map_opt.max_chain_iter = m_params.max_chain_iter;
  m_map_opt.max_clip_ratio = m_params.max_clip_ratio;
  m_map_opt.zdrop = m_params.zdrop;
  m_map_opt.zdrop_inv = m_params.zdrop_inv;
  m_map_opt.max_gap_ref = m_params.max_gap_ref;
  m_map_opt.a = m_params.a;
  m_map_opt.b = m_params.b;

#ifdef DEBUG_LOCAL_ALN
  std::cerr << "max_join_short: " << m_params.max_join_short << std::endl
            << "max_join_long: " << m_params.max_join_long << std::endl
            << "min_join_flank_sc: " << m_params.min_join_flank_sc << std::endl
            << "min_join_flank_ratio: " << m_params.min_join_flank_ratio << std::endl
            << "max_gap: " << m_params.max_gap << std::endl
            << "max_gap_ref: " << m_params.max_gap_ref << std::endl
            << "bw: " << m_params.bw << std::endl
            << "pri_ratio: " << m_params.pri_ratio << std::endl
            << "max_chain_skip: " << m_params.max_chain_skip << std::endl
            << "max_chain_iter: " << m_params.max_chain_iter << std::endl
            << "min_chain_score: " << m_params.min_chain_score << std::endl
            << "chain_gap_scale: " << m_params.chain_gap_scale << std::endl
            << "a: " << m_params.a << " b: " << m_params.b << std::endl
            << "e: " << m_params.e << " e2: " << m_params.e2 << std::endl
            << "q: " << m_params.q << " q2: " << m_params.q2 << std::endl
            << "end_bonus: " << m_params.end_bonus << std::endl
            << "max_clip_ratio: " << m_params.max_clip_ratio << std::endl
            << "zdrop: " << m_params.zdrop << " zdrop_inv: " << m_params.zdrop_inv << std::endl
            << "minimizer_w " << m_params.minimizer_w << " minimizer_k: " << m_params.minimizer_k << std::endl
            << "bucket_bits: " << m_params.bucket_bits << std::endl
            << "is_hpc: " << m_params.is_hpc << std::endl;
#endif
  m_minimap_index = mm_idx_str(m_params.minimizer_w,
                               m_params.minimizer_k,
                               m_params.is_hpc,
                               m_params.bucket_bits, 1,
                               (const char **)&m_local_sequence, NULL);
  // update the mapping options
  mm_mapopt_update(&m_map_opt, m_minimap_index);
  mm_idx_stat(m_minimap_index);
}

LocalAlignment::~LocalAlignment() {
  // free allocated memory
  mm_idx_destroy(m_minimap_index);
  free(m_local_sequence);

  for (auto &aln : m_alignments) {
    for (int j = 0; j < aln.second.num_hits; ++j)
      free(aln.second.reg[j].p);
    free(aln.second.reg);
  }
}

void LocalAlignment::align(const SeqLib::UnalignedSequenceVector &seqs) {
  mm_tbuf_t *thread_buf = mm_tbuf_init();
  for (auto &seq : seqs) {
    MinimapAlignment alignment;
    alignment.reg =
        mm_map(m_minimap_index, seq.Seq.length(), seq.Seq.c_str(),
               &alignment.num_hits, thread_buf, &m_map_opt, seq.Name.c_str());
    m_alignments[seq] = alignment;
  }
  mm_tbuf_destroy(thread_buf);
}

size_t LocalAlignment::writeAlignments(std::ostream &out) {
  for (auto &aln : m_alignments) {
    int num_hits = aln.second.num_hits;
    mm_reg1_t *reg = aln.second.reg;
    SeqLib::UnalignedSequence seq = aln.first;

    for (int j = 0; j < num_hits; ++j) { // traverse hits and inspect them
      // Query name, query length, query start, query end
      std::stringstream query_record;
      // Target name, target length, target start, target end
      std::stringstream target_record;
      // Data for the current hit
      std::stringstream hit_record;

      query_record << seq.Name.c_str() << " " << seq.Seq.length() << " ";

      target_record << m_target_name << " " << m_minimap_index->seq->len << " ";

      hit_record << j << " "
                 << "+-"[reg->rev] << " ";

      mm_reg1_t *r = &reg[j];
      assert(r->p); // with MM_F_CIGAR, this should not be NULL
      for (int i = 0; i < r->p->n_cigar; ++i)
        hit_record << (r->p->cigar[i] >> 4) << ("MIDNSH"[r->p->cigar[i] & 0xf]);

      query_record << (r->qs) << " " << (r->qe) << " ";
      target_record << (r->rs) << " " << (r->re) << " ";

      // put together the alignment summary
      out << target_record.str() << query_record.str() << hit_record.str()
          << std::endl;
    }
  }
  return m_alignments.size();
}

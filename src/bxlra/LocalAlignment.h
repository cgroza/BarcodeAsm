#ifndef LOCAL_ALGIGNMENT_H
#define LOCAL_ALGIGNMENT_H

#include "SeqLib/BWAWrapper.h"
#include "SeqLib/BamRecord.h"
#include "SeqLib/GenomicRegion.h"
#include "SeqLib/RefGenome.h"
#include "SeqLib/UnalignedSequence.h"
#include "minimap2/minimap.h"
#include <stdlib.h>
#include <cstring>
#include <unordered_map>
#include <ostream>

struct MinimapAlignment {
    mm_reg1_t* reg;
    int num_hits;
};

struct UnalignedSequenceHash {
  std::size_t operator()(const SeqLib::UnalignedSequence &k) const {
    return std::hash<std::string>()(k.Seq);
  }
};

struct UnalignedSequenceEqualsTo {
  bool operator()(const SeqLib::UnalignedSequence &a,
                  const SeqLib::UnalignedSequence b) const {
    return std::equal_to<std::string>()(a.Seq, b.Seq) &&
           std::equal_to<std::string>()(a.Name, b.Name);
  }
};

class LocalAlignment {
public:
    LocalAlignment(std::string chr, size_t start, size_t end,
                   const SeqLib::RefGenome &genome);
    LocalAlignment(std::string target_sequence);

    ~LocalAlignment();
    void align(const SeqLib::UnalignedSequenceVector &seqs);
    size_t writeAlignments(std::ostream &out);

    // default minimap2 parameters
    const int MINIMIZER_K = 15;
    const int MINIMIZER_W = 10;
    const int BUCKET_BITS = 64;
    const int IS_HPC = 0;

private:
    void setupIndex(std::string target_sequence);

    mm_idx_t* m_minimap_index;
    mm_idxopt_t m_index_opt;
    mm_mapopt_t m_map_opt;

    char *m_local_sequence;

    std::unordered_map<SeqLib::UnalignedSequence,
                       MinimapAlignment,
                       UnalignedSequenceHash,
                       UnalignedSequenceEqualsTo> m_alignments;
};

#endif
import pysam
import os
import queue
import math
import tempfile
import argparse
import multiprocessing as mp
from Bio import pairwise2
from Bio.Align.Applications import MuscleCommandline
from Bio import AlignIO
from Bio.Align import AlignInfo

def is_from_locus(name, locus):
    return locus == '_'.join(name.split("_")[0:3])

def locus_from_name(name):
    return '_'.join(name.split("_")[0:3])

def build_locus_index(fasta_file):
    index = {}
    # create buckets for each locus
    for name in contig_fasta.references:
        index[locus_from_name(name)] = []
    # add contigs to the right locus bucket
    for name in contig_fasta.references:
        index[locus_from_name(name)].append(name)
    return index

def run_loci(contig_fasta_path, out_msa, muscle_exe, input_queue, output_queue):
    contig_fasta = pysam.FastaFile(contig_fasta_path)
    try:
        while True:
            (locus, contigs) = input_queue.get_nowait()
            # print("Running", locus, "with", len(locus_index[locus]), "sequences")
            (in_f_desc, in_path) = tempfile.mkstemp(text = True)
            in_f = os.fdopen(in_f_desc, mode = "w")

            out_path = os.path.join(out_msa, locus)  + ".fa"

            # don't do MUSCLE for loci that have only a single contig
            if len(contigs) < 2:
                var = contig_fasta.fetch(contigs[0])
                output_queue.put((locus, var))
                input_queue.task_done()
                continue

            for contig in contigs:
                var = contig_fasta.fetch(contig)
                in_f.writelines([">" + contig + "\n", var + "\n"])

            in_f.close()

            muscle_cline = MuscleCommandline(muscle_exe, input = in_path, out = out_path)
            muscle_cline()
            alignment = AlignIO.read(out_path, 'fasta')
            summary_align = AlignInfo.SummaryInfo(alignment)
            cons = summary_align.dumb_consensus(threshold = 0.5, ambiguous = "N")

            output_queue.put((locus, str(cons)))

            os.remove(in_path)
            input_queue.task_done()
            # os.remove(out_path)
    except queue.Empty:
        print("Done", mp.current_process().name)
        return True

def write_results(results_queue, output_path):
    # output consensus sequences
    print("Writing to", output_path)
    out_f = open(output_path, "w")
    try:
        while True:
            cons_contig = results_queue.get_nowait()
            out_f.writelines([">" + cons_contig[0] + "\n", cons_contig[1] + "\n"])
            results_queue.task_done()
    except queue.Empty:
        out_f.close()
        print("Wrote to", output_path)
        return True

if __name__ == '__main__':
    parser = argparse.ArgumentParser("Build consensus sequence from assembled contigs.")
    parser.add_argument("--contigs", metavar="contigs", type = str, nargs = 1,
                        help = "Input contigs for MSA generated by bxlra")
    parser.add_argument("--out_cons", metavar="out", type = str, nargs = 1,
                        help = "Output path for consensus sequences.")
    parser.add_argument("--out_msa", metavar="out", type = str, nargs = 1,
                        help = "Output directory for MSAs.")
    args = parser.parse_args()

    msa_out_dir = args.out_msa[0]
    try:
        os.mkdir(msa_out_dir)
    except FileExistsError:
        pass

    contig_fasta_path = args.contigs[0]
    contig_fasta = pysam.FastaFile(contig_fasta_path)
    muscle_exe = "/Users/cgroza/bin/muscle3.8.31_i86darwin64"
    locus_index = build_locus_index(contig_fasta)

    n_loci = len(locus_index)

    input_queue = mp.JoinableQueue()
    output_queue = mp.JoinableQueue()


    print("Found", n_loci, "loci")

    cpus = mp.cpu_count()
    loci = list(locus_index.keys())

    # creating task queue
    print("Pushing loci into queue")
    for locus in locus_index:
        input_queue.put((locus, locus_index[locus]))

    print("Spawning", cpus, "worker threads")
    procs = []
    for i in range(cpus):
        p = mp.Process(target = run_loci, args = (contig_fasta_path, msa_out_dir, muscle_exe, input_queue, output_queue))
        procs.append(p)
        p.start()

    # wait for all the tasks to finish feeding to child processes
    # note: this does not mean that the child processes finished
    print("Waiting for threads to finish...")
    input_queue.join()
    print("Input queue empty")

    # output results in a separate process that counts tasks until the output queue is empty for good
    out_p = mp.Process(target = write_results, args = (output_queue, args.out_cons[0]))
    out_p.start()
    # wait for all the tasks to be finished by the child processes
    output_queue.join()
    print("Output queue empty")
    print("Finished")

    # join child processes and exit
    out_p.join()
    for p in procs:
        p.join()
    exit(0)
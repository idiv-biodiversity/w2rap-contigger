//
// Created by Bernardo Clavijo (TGAC) on 21/06/2016.
//
#include <paths/long/large/Repath.h>
#include <paths/long/large/Clean200.h>
#include <paths/long/large/Simplify.h>
#include <paths/long/large/MakeGaps.h>
#include <paths/long/large/FinalFiles.h>
#include "FastIfstream.h"
#include "FetchReads.h"
#include "MainTools.h"
#include "PairsManager.h"
#include "ParallelVecUtilities.h"
#include "feudal/PQVec.h"
#include "lookup/LookAlign.h"
#include "paths/HyperBasevector.h"
#include "paths/RemodelGapTools.h"
#include "paths/long/BuildReadQGraph.h"
#include "paths/long/PlaceReads0.h"
#include "paths/long/SupportedHyperBasevector.h"
#include "paths/long/large/AssembleGaps.h"
#include "paths/long/large/ExtractReads.h"
#include "tclap/CmdLine.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/time.h>


std::string checkpoint_perf_time(const std::string section_name){
    static double wtimer, cputimer;
    double new_wtimer, wtime, new_cputimer, cputime;
    //wallclock time
    struct timeval time;
    if (gettimeofday(&time,NULL)) new_wtimer=0;
    else new_wtimer = (double)time.tv_sec + (double)time.tv_usec * .000001;
    //cpu_time
    new_cputimer= (double)clock() / CLOCKS_PER_SEC;
    wtime=new_wtimer-wtimer;
    cputime=new_cputimer-cputimer;
    wtimer=new_wtimer;
    cputimer=new_cputimer;
    return "TIME, "+section_name+", "+std::to_string(wtime)+", "+std::to_string(cputime);
}

int main(const int argc, const char * argv[]) {

    std::string out_prefix;
    std::string read_files;
    std::string out_dir;
    unsigned int threads;
    int max_mem;
    unsigned int small_K, large_K, min_size,from_step,to_step;
    std::vector<unsigned int> allowed_k = {60, 64, 72, 80, 84, 88, 96, 100, 108, 116, 128, 136, 144, 152, 160, 168, 172,
                                           180, 188, 192, 196, 200, 208, 216, 224, 232, 240, 260, 280, 300, 320, 368,
                                           400, 440, 460, 500, 544, 640};
    std::vector<unsigned int> allowed_steps = {1,2,3,4,5,6,7};
    bool extend_paths,dump_all,dump_perf;

    //========== Command Line Option Parsing ==========

    std::cout << "Welcome to w2rap-contigger" << std::endl;
    try {
        TCLAP::CmdLine cmd("", ' ', "0.1");
        TCLAP::ValueArg<unsigned int> threadsArg("t", "threads",
             "Number of threads on parallel sections (default: 4)", false, 4, "int", cmd);
        TCLAP::ValueArg<unsigned int> max_memArg("m", "max_mem",
             "Maximum memory in GB (soft limit, impacts performance, default 10000)", false, 10000, "int", cmd);

        TCLAP::ValueArg<std::string> read_filesArg("r", "read_files",
             "Input sequences (reads) files ", true, "", "file1.fastq,file2.fastq", cmd);

        TCLAP::ValueArg<std::string> out_dirArg("o", "out_dir", "Output dir path", true, "", "string", cmd);
        TCLAP::ValueArg<std::string> out_prefixArg("p", "prefix",
             "Prefix for the output files", true, "", "string", cmd);

        TCLAP::ValuesConstraint<unsigned int> largeKconst(allowed_k);
        TCLAP::ValueArg<unsigned int> large_KArg("K", "large_k",
             "Large k (default: 200)", false, 200, &largeKconst, cmd);
        //TCLAP::ValueArg<unsigned int> small_KArg("k", "small_k",
        //                                         "Small k (default: 60)", false, 60, &largeKconst, cmd);

        TCLAP::ValuesConstraint<unsigned int> steps(allowed_steps);
        TCLAP::ValueArg<unsigned int> fromStep_Arg("", "from_step",
                                                 "Start on step (default: 1)", false, 1, &steps, cmd);

        TCLAP::ValueArg<unsigned int> toStep_Arg("", "to_step",
                                                   "Stop after step (default: 7)", false, 7, &steps, cmd);

        TCLAP::ValueArg<unsigned int> minSizeArg("s", "min_size",
             "Min size of disconnected elements on large_k graph (in kmers, default: 0=no min)", false, 0, "int", cmd);
        TCLAP::ValueArg<bool>         pathExtensionArg        ("","extend_paths",
             "Enable extend paths on repath (experimental)", false,false,"bool",cmd);
        TCLAP::ValueArg<bool>         dumpAllArg        ("","dump_all",
                                                               "Dump all intermediate files", false,false,"bool",cmd);
        TCLAP::ValueArg<bool>         dumpPerfArg        ("","dump_perf",
                                                         "Dump performance info (devel)", false,false,"bool",cmd);
        cmd.parse(argc, argv);

        // Get the value parsed by each arg.
        out_dir = out_dirArg.getValue();
        out_prefix = out_prefixArg.getValue();
        read_files = read_filesArg.getValue();
        threads = threadsArg.getValue();
        max_mem = max_memArg.getValue();
        large_K = large_KArg.getValue();
        small_K = 60;//small_KArg.getValue();
        min_size = minSizeArg.getValue();
        extend_paths=pathExtensionArg.getValue();
        dump_all=dumpAllArg.getValue();
        dump_perf=dumpPerfArg.getValue();
        from_step=fromStep_Arg.getValue();
        to_step=toStep_Arg.getValue();

    } catch (TCLAP::ArgException &e)  // catch any exceptions
    {
        std::cerr << "error: " << e.error() << " for arg " << e.argId() << std::endl;
        return 1;
    }

    //Check directory exists:


    struct stat info;

    if (stat(out_dir.c_str(), &info) != 0 || !(info.st_mode & S_IFDIR)) {
        std::cout << "Output directory doesn't exist, or is not a directory: " << out_dir << std::endl;
        return 1;
    }

    //========== Main Program Begins ======

    //== Set computational resources ===
    SetThreads(threads, False);
    SetMaxMemory(int64_t(round(max_mem * 1024.0 * 1024.0 * 1024.0)));


    //== Load reads (and saves in binary format) ======
    vecbvec bases;
    VecPQVec quals;

    vec<String> subsam_names = {"C"};
    vec<int64_t> subsam_starts = {0};
    std::ofstream perf_file;
    double wtimer,cputimer;
    if (dump_perf) {
        perf_file.open(out_dir+"/"+out_prefix+".perf",std::ofstream::out | std::ofstream::app);
    }
    if (dump_perf) checkpoint_perf_time(""); //initialisation!

    if (from_step==1)
    {
        std::cout << "--== Step 1: Reading input files ==--" << std::endl;
        ExtractReads(read_files, out_dir, subsam_names, subsam_starts, &bases, &quals);
        std::cout << "Reading input files DONE!" << std::endl << std::endl << std::endl;
        if (dump_perf) perf_file << checkpoint_perf_time("ExtractReads") << std::endl;
        //TODO: add an option to dump the reads
        if (dump_all || to_step<6) {
            std::cout << "Dumping reads in fastb/qualp format..." << std::endl;
            bases.WriteAll(out_dir + "/frag_reads_orig.fastb");
            quals.WriteAll(out_dir + "/frag_reads_orig.qualp");
            std::cout << "   DONE!" << std::endl;
            if (dump_perf) perf_file << checkpoint_perf_time("DumpReads") << std::endl;
        }
    }

    //== Read QGraph, and repath (k=60, k=200 (and saves in binary format) ======
    bool FILL_JOIN = False;
    bool SHORT_KMER_READ_PATHER = False;
    bool RQGRAPHER_VERBOSE = False;
    vec<int> inv;
    HyperBasevector hbvr;
    ReadPathVec pathsr;
    if (from_step>1 && from_step<7){
        std::cout << "Loading reads in fastb/qualp format..." << std::endl;
        bases.ReadAll(out_dir + "/frag_reads_orig.fastb");
        quals.ReadAll(out_dir + "/frag_reads_orig.qualp");
        std::cout << "   DONE!" << std::endl;
        if (dump_perf) perf_file << checkpoint_perf_time("LoadReads") << std::endl;
    }
    {//This scope-trick to invalidate old data is dirty
        HyperBasevector hbv;
        ReadPathVec paths;
        if (from_step<=2 and to_step>=2) {
            std::cout << "--== Step 2: Building first (small K) graph ==--" << std::endl;
            buildReadQGraph(bases, quals, FILL_JOIN, FILL_JOIN, 7, 3, .75, 0, "", True, SHORT_KMER_READ_PATHER, &hbv,
                            &paths,
                            small_K);
            if (dump_perf) perf_file << checkpoint_perf_time("buildReadQGraph") << std::endl;
            FixPaths(hbv, paths); //TODO: is this even needed?
            if (dump_perf) perf_file << checkpoint_perf_time("FixPaths") << std::endl;
            std::cout << "Building first graph DONE!" << std::endl << std::endl << std::endl;
            if (dump_all || to_step ==2){
                std::cout << "Dumping small_K graph and paths..." << std::endl;
                BinaryWriter::writeFile(out_dir + "/" + out_prefix + ".small_K.hbv", hbv);
                paths.WriteAll(out_dir + "/" + out_prefix + ".small_K.paths");
                std::cout << "   DONE!" << std::endl;
                if (dump_perf) perf_file << checkpoint_perf_time("SmallKDump") << std::endl;
            }
        }

        if (from_step==3){
            std::cout << "Reading small_K graph and paths..." << std::endl;
            BinaryReader::readFile(out_dir + "/" + out_prefix + ".small_K.hbv", &hbv);
            paths.ReadAll(out_dir + "/" + out_prefix + ".small_K.paths");
            std::cout << "   DONE!" << std::endl;
            if (dump_perf) perf_file << std::endl << checkpoint_perf_time("SmallKLoad") << std::endl;
        }
        if (from_step<=3 and to_step>=3) {
            std::cout << "--== Step 3: Repathing to second (large K) graph ==--" << std::endl;
            vecbvec edges(hbv.Edges().begin(), hbv.Edges().end());
            inv.clear();
            hbv.Involution(inv);
            if (dump_perf) perf_file << checkpoint_perf_time("Edges&Involution") << std::endl;
            FragDist(hbv, inv, paths, out_dir + "/" + out_prefix + ".first.frags.dist");
            if (dump_perf) perf_file << checkpoint_perf_time("FragDist") << std::endl;
            const string run_head = out_dir + "/" + out_prefix;

            pathsr.resize(paths.size());
            RepathInMemory(hbv, edges, inv, paths, hbv.K(), large_K, hbvr, pathsr, True, True, extend_paths);
            if (dump_perf) perf_file << checkpoint_perf_time("Repath") << std::endl;
            std::cout << "Repathing to second graph DONE!" << std::endl << std::endl << std::endl;
            if (dump_all || to_step ==3){
                std::cout << "Dumping large_K graph and paths..." << std::endl;
                BinaryWriter::writeFile(out_dir + "/" + out_prefix + ".large_K.hbv", hbvr);
                pathsr.WriteAll(out_dir + "/" + out_prefix + ".large_K.paths");
                std::cout << "   DONE!" << std::endl;
                if (dump_perf) perf_file << checkpoint_perf_time("LargeKDump") << std::endl;
            }
        }

    }

    //== Clean ======
    if (from_step==4){
        std::cout << "Reading large_K graph and paths..." << std::endl;
        BinaryReader::readFile(out_dir + "/" + out_prefix + ".large_K.hbv", &hbvr);
        pathsr.ReadAll(out_dir + "/" + out_prefix + ".large_K.paths");
        std::cout << "   DONE!" << std::endl;
        if (dump_perf) perf_file << std::endl << checkpoint_perf_time("LargeKLoad") << std::endl;
    }
    if (from_step<=4 and to_step>=4) {
        std::cout << "--== Step 4: Cleaning graph ==--" << std::endl;
        inv.clear();
        hbvr.Involution(inv);
        int CLEAN_200_VERBOSITY = 0;
        int CLEAN_200V = 3;
        Clean200x(hbvr, inv, pathsr, bases, quals, CLEAN_200_VERBOSITY, CLEAN_200V, min_size);
        if (dump_perf) perf_file << checkpoint_perf_time("Clean200x") << std::endl;
        std::cout << "Cleaning graph DONE!" << std::endl<< std::endl<< std::endl;
        if (dump_all || to_step ==4){
            std::cout << "Dumping large_K clean graph and paths..." << std::endl;
            BinaryWriter::writeFile(out_dir + "/" + out_prefix + ".large_K.clean.hbv", hbvr);
            pathsr.WriteAll(out_dir + "/" + out_prefix + ".large_K.clean.paths");
            std::cout << "   DONE!" << std::endl;
            if (dump_perf) perf_file << checkpoint_perf_time("LargeKCleanDump") << std::endl;
        }
    }

    //== Patching ======

    VecULongVec paths_inv;

    if (from_step==5){
        std::cout << "Reading large_K clean graph and paths..." << std::endl;
        BinaryReader::readFile(out_dir + "/" + out_prefix + ".large_K.clean.hbv", &hbvr);
        pathsr.ReadAll(out_dir + "/" + out_prefix + ".large_K.clean.paths");
        inv.clear();
        hbvr.Involution(inv);
        std::cout << "   DONE!" << std::endl;
        if (dump_perf) perf_file << std::endl << checkpoint_perf_time("LargeKCleanLoad") << std::endl;
    }
    if (from_step<=5 and to_step>=5) {
        std::cout << "--== Step 5: Assembling gaps ==--" << std::endl;

        invert(pathsr, paths_inv, hbvr.EdgeObjectCount());
        if (dump_perf) perf_file << checkpoint_perf_time("Invert") << std::endl;

        vecbvec new_stuff;

        bool EXTEND = False;
        bool ANNOUNCE = False;
        bool KEEP_ALL_LOCAL = False;
        bool CONSERVATIVE_KEEP = False;
        bool INJECT = False;
        bool LOCAL_LAYOUT = False;
        const String DUMP_LOCAL = "";
        int K2_FLOOR = 0;
        int DUMP_LOCAL_LROOT = -1;
        int DUMP_LOCAL_RROOT = -1;
        bool CYCLIC_SAVE = True;
        int A2V = 5;
        int GAP_CAP = -1;
        int MAX_PROX_LEFT = 400;
        int MAX_PROX_RIGHT = 400;
        int MAX_BPATHS = 100000;

        AssembleGaps2(hbvr, inv, pathsr, paths_inv, bases, quals, out_dir, EXTEND, ANNOUNCE, KEEP_ALL_LOCAL,
                      CONSERVATIVE_KEEP, INJECT, LOCAL_LAYOUT, DUMP_LOCAL, K2_FLOOR, DUMP_LOCAL_LROOT, DUMP_LOCAL_RROOT,
                      new_stuff, CYCLIC_SAVE, A2V, GAP_CAP, MAX_PROX_LEFT, MAX_PROX_RIGHT, MAX_BPATHS);
        if (dump_perf) perf_file << checkpoint_perf_time("AssembleGaps2") << std::endl;
        int MIN_GAIN = 5;
        //const String TRACE_PATHS="{}";
        const vec<int> TRACE_PATHS;
        int EXT_MODE = 1;

        AddNewStuff(new_stuff, hbvr, inv, pathsr, bases, quals, MIN_GAIN, TRACE_PATHS, out_dir, EXT_MODE);
        PartnersToEnds(hbvr, pathsr, bases, quals);
        if (dump_perf) perf_file << checkpoint_perf_time("NewStuff&Partners") << std::endl;
        std::cout << "Assembling gaps DONE!" << std::endl << std::endl << std::endl;
        if (dump_all || to_step ==5){
            std::cout << "Dumping large_K final graph and paths..." << std::endl;
            BinaryWriter::writeFile(out_dir + "/" + out_prefix + ".large_K.final.hbv", hbvr);
            pathsr.WriteAll(out_dir + "/" + out_prefix + ".large_K.final.paths");
            std::cout << "   DONE!" << std::endl;
            if (dump_perf) perf_file << checkpoint_perf_time("LargeKFinalDump") << std::endl;
        }

    }

    int MAX_CELL_PATHS = 50;
    int MAX_DEPTH = 10;

    if (from_step==6){
        std::cout << "Reading large_K final graph and paths..." << std::endl;
        BinaryReader::readFile(out_dir + "/" + out_prefix + ".large_K.final.hbv", &hbvr);
        pathsr.ReadAll(out_dir + "/" + out_prefix + ".large_K.final.paths");
        inv.clear();
        hbvr.Involution(inv);
        std::cout << "   DONE!" << std::endl;
        if (dump_perf) perf_file << std::endl << checkpoint_perf_time("LargeKFinalLoad") << std::endl;
    }
    if (from_step<=6 and to_step>=6) {
        std::cout << "--== Step 6: Contigging ==--" << std::endl;
        //==Simplify
        int MAX_SUPP_DEL = 0;
        bool TAMP_EARLY_MIN = True;
        int MIN_RATIO2 = 8;
        int MAX_DEL2 = 200;
        bool PLACE_PARTNERS = False;
        bool ANALYZE_BRANCHES_VERBOSE2 = False;
        const String TRACE_SEQ = "";
        bool DEGLOOP = True;
        bool EXT_FINAL = True;
        int EXT_FINAL_MODE = 1;
        bool PULL_APART_VERBOSE = False;
        //const String PULL_APART_TRACE="{}";
        const vec<int> PULL_APART_TRACE;
        int DEGLOOP_MODE = 1;
        float DEGLOOP_MIN_DIST = 2.5;
        bool IMPROVE_PATHS = True;
        bool IMPROVE_PATHS_LARGE = False;
        bool FINAL_TINY = True;
        bool UNWIND3 = True;

        Simplify(out_dir, hbvr, inv, pathsr, bases, quals, MAX_SUPP_DEL, TAMP_EARLY_MIN, MIN_RATIO2, MAX_DEL2,
                 PLACE_PARTNERS, ANALYZE_BRANCHES_VERBOSE2, TRACE_SEQ, DEGLOOP, EXT_FINAL, EXT_FINAL_MODE,
                 PULL_APART_VERBOSE, PULL_APART_TRACE, DEGLOOP_MODE, DEGLOOP_MIN_DIST, IMPROVE_PATHS,
                 IMPROVE_PATHS_LARGE, FINAL_TINY, UNWIND3);
        if (dump_perf) perf_file << checkpoint_perf_time("Simplify") << std::endl;
        // For now, fix paths and write the and their inverse
        for (int i = 0; i < (int) pathsr.size(); i++) { //XXX TODO: change this int for uint 32
            Bool bad = False;
            for (int j = 0; j < (int) pathsr[i].size(); j++)
                if (pathsr[i][j] < 0) bad = True;
            if (bad) pathsr[i].resize(0);
        }
        // TODO: this is "bj making sure the inversion still works", but shouldn't be required
        paths_inv.clear();
        invert(pathsr, paths_inv, hbvr.EdgeObjectCount());
        if (dump_perf) perf_file << checkpoint_perf_time("Fix&Invert") << std::endl;

        // Find lines and write files.
        vec<vec<vec<vec<int>>>> lines;

        FindLines(hbvr, inv, lines, MAX_CELL_PATHS, MAX_DEPTH);
        if (dump_perf) perf_file << checkpoint_perf_time("FindLines") << std::endl;
        BinaryWriter::writeFile(out_dir + "/" + out_prefix + ".fin.lines", lines);

        // XXX TODO: Solve the {} thingy, check if has any influence in the new code to run that integrated
        {
            vec<int> llens, npairs;
            GetLineLengths(hbvr, lines, llens);
            GetLineNpairs(hbvr, inv, pathsr, lines, npairs);
            BinaryWriter::writeFile(out_dir + "/" + out_prefix + ".fin.lines.npairs", npairs);

            vec<vec<covcount>> covs;
            ComputeCoverage(hbvr, inv, pathsr, lines, subsam_starts, covs);
            //BinaryWriter::writeFile( work_dir + "/" +prefix+ ".fin.covs", covs );
            //WriteLineStats( work_dir, lines, llens, npairs, covs );

            // Report CN stats
            double cn_frac_good = CNIntegerFraction(hbvr, covs);
            std::cout << "CN fraction good = " << cn_frac_good << std::endl;
            PerfStatLogger::log("cn_frac_good", ToString(cn_frac_good, 2), "fraction of edges with CN near integer");
        }
        if (dump_perf) perf_file << checkpoint_perf_time("LineStats") << std::endl;

        // TestLineSymmetry( lines, inv2 );
        // Compute fragment distribution.
        FragDist(hbvr, inv, pathsr, out_dir + "/" + out_prefix + ".fin.frags.dist");
        if (dump_perf) perf_file << checkpoint_perf_time("FragDist") << std::endl;
        //TODO: add contig fasta dump.
        std::cout << "Contigging DONE!" << std::endl << std::endl << std::endl;
        if (dump_all || to_step == 6){
            std::cout << "Dumping contig graph and paths..." << std::endl;
            BinaryWriter::writeFile(out_dir + "/" + out_prefix + ".contig.hbv", hbvr);
            pathsr.WriteAll(out_dir + "/" + out_prefix + ".contig.paths");
            std::cout << "   DONE!" << std::endl;
            if (dump_perf) perf_file << checkpoint_perf_time("ContigGraphDump") << std::endl;
        }

    }
    if (from_step==7){
        std::cout << "Reading contig graph and paths..." << std::endl;
        BinaryReader::readFile(out_dir + "/" + out_prefix + ".contig.hbv", &hbvr);
        pathsr.ReadAll(out_dir + "/" + out_prefix + ".contig.paths");
        inv.clear();
        hbvr.Involution(inv);
        paths_inv.clear();
        invert(pathsr, paths_inv, hbvr.EdgeObjectCount());
        std::cout << "   DONE!" << std::endl;
        if (dump_perf) perf_file << std::endl << checkpoint_perf_time("ContigGraphLoad") << std::endl;
    }
    if (from_step<=7 and to_step>=7) {
        //== Scaffolding
        std::cout << "--== Step 7: PE-Scaffolding ==--" << std::endl;
        int MIN_LINE = 5000;
        int MIN_LINK_COUNT = 3; //XXX TODO: this variable is the same as -w in soap??

        bool SCAFFOLD_VERBOSE = False;
        bool GAP_CLEANUP = True;
        MakeGaps(hbvr, inv, pathsr, paths_inv, MIN_LINE, MIN_LINK_COUNT, out_dir, out_prefix, SCAFFOLD_VERBOSE,
                 GAP_CLEANUP);
        if (dump_perf) perf_file << checkpoint_perf_time("MakeGaps") << std::endl;
        std::cout << "--== PE-Scaffolding DONE!" << std::endl << std::endl << std::endl;
        // Carry out final analyses and write final assembly files.

        vecbasevector G;
        FinalFiles(hbvr, inv, pathsr, subsam_names, subsam_starts, out_dir, MAX_CELL_PATHS, MAX_DEPTH, G);
        if (dump_perf) perf_file << checkpoint_perf_time("FinalFiles") << std::endl;


    }
    if (dump_perf) perf_file.close();
    return 0;
}

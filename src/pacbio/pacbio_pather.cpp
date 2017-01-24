//
// Created by Gonzalo Garcia (TGAC) on 04/11/2016.
//

#include "pacbio_pather.h"


PacbioPather::PacbioPather(vecbvec& aseqVector, HyperBasevector& ahbv, vec<int>& ainv, int min_reads, std::vector<BaseVec>& edges, ReadPathVec& apaths, VecULongVec& ainvPaths)
    : KMatch(31), seqVector(aseqVector), mEdges(edges), PathFinder(ahbv, ainv, apaths, ainvPaths, 5) { }

std::vector<std::vector<linkReg>> PacbioPather::getReadsLinks(bool output_to_file=true){
  // Get the reads
  std::vector<std::vector<linkReg>> links;

  std::cout<< Date() << ": Size of the dictionary: " << edgeMap.size() << std::endl;

  std::cout<< Date() << ": loading edges and involution." << std::endl;
  auto edges = mHBV.Edges();
  std::cout << "edges loaded" << std::endl;
  vec<int> inv;
  mHBV.Involution(inv);

  std::cout<< Date() << ": processing edges" << std::endl;
//  int cont = 0;
  links.resize(seqVector.size());
#pragma omp parallel for
  for (auto v=0; v<seqVector.size(); ++v){
    std::string read = seqVector[v].ToString();
    auto g = lookupRead(read);
    if (g.size()>0){
      for (size_t a=0; a<g.size(); ++a){
        linkReg temp_link;
        temp_link.read_id = v;
        temp_link.read_size = read.size();
        temp_link.read_offset = g[a].read_offset;
        temp_link.edge_id = g[a].edge_id;
        temp_link.edge_offset = g[a].edge_offset;
        temp_link.inv_edge_id = inv[g[a].edge_id];
        temp_link.kmer = g[a].kmer;
#pragma omp critical
        links[v].push_back(temp_link);
      }
    }
  }

  return links;
}

std::vector<linkReg> PacbioPather::readOffsetFilter(std::vector<linkReg> data){
  // Count the number of edges_id that map to each kmer in the read, filter out the edges that map to a position that has more than one edge mapped to it
  // input is a vector with all the links for a specific read

  std::vector<linkReg> links;
  std::map<int, unsigned int> pos_count;

  for (auto l: data){
      // Aumentar el contador, valores son inicializados a 0!?
      pos_count[l.read_offset] += 1;
  }

  // Solo seleccionados lo que estan en offsets de la lectura con un solo edge mapeado
  for (auto l: data){
      if (pos_count[l.read_offset] == 1){
        links.push_back(l);
      }
  }
  return links;
}

std::vector<linkReg> PacbioPather::matchLengthFilter(std::vector<linkReg> data){
  // Filter matches by length of the match

  std::map<std::string, int> index_map;
  for (auto l: data){
    // juntar la lectura y el eje en el mismo string concatenando asi pueod indexar por los dos
    std::string key = l.read_id + "-" +l.edge_id;
    index_map[key] += 1;
  }

  // contar los links por lectura y por edge
  std::vector<std::string> filtered_links;
  for (std::map<std::string, int>::iterator k=index_map.begin(); k != index_map.end(); ++k){
    //
    if (k->second > 10) {
      filtered_links.push_back(k->first);
    }
  }

  // Filtrar los que no pegan mas del limite
  std::vector<linkReg> good_links;
  for (auto l: data){
    std::string key = l.read_id + "-" +l.edge_id;
    if (std::find(filtered_links.begin(), filtered_links.end(), key) != filtered_links.end()){
      good_links.push_back(l);
    }
  }

  //devolver la lista de filtrados
  return good_links;
}

ReadPathVec PacbioPather::mapReads(){
  // get the reads and map them to the graph using the dictionary
  // returns a vector of paths
  std::cout << Date()<<": Executing getReadsLines..." << std::endl;
  auto links = getReadsLinks(true);
  std::cout << Date() << ": Done, " << links.size() << " raw links recovered" << std::endl;

  // To store the paths
  ReadPath pb_paths_temp[seqVector.size()];
  ReadPathVec pb_paths;

  // for each read
  std::cout<<Date()<<": pathing "<<seqVector.size()<<" PacBio reads..."<<std::endl;
  std::atomic_uint_fast64_t pr(0),ppr(0);
  //#pragma omp parallel for
  for (std::uint32_t r=0; r<seqVector.size(); ++r){

    // get the links for this reads
    auto read_links = links[r];

    // filter the shared roffsets
    auto offset_filter = readOffsetFilter(read_links);

    // filter the min match length
    auto minmatchFilter = matchLengthFilter(offset_filter);

    // sort the vector
    std::sort(minmatchFilter.begin(), minmatchFilter.end(), linkreg_less_than_pb());

    // Create vector of unique edge_ids
    std::vector<int> presentes;
    std::vector<linkReg> s_edges;
    for (auto a: minmatchFilter){
      if (std::find(presentes.begin(), presentes.end(), a.edge_id) == presentes.end()){
        s_edges.push_back(a);
        presentes.push_back(a.edge_id);
      }
    }

    std::vector<int> temp_path;
    if (s_edges.size() >= 2) {

      int poffset=s_edges[0].edge_offset;
      int read_size=s_edges[0].read_size;

      for (auto s: s_edges) {
        temp_path.push_back(s.edge_id);
      }
      pb_paths_temp[ppr++]=ReadPath(poffset, temp_path);
    }
    ++pr;
  }
  std::cout<<Date()<<": "<<pr<<" reads processed, "<<ppr<<" pathed"<<std::endl;
  pb_paths.reserve(ppr);
  for (auto i=0;i<ppr;++i) pb_paths.push_back(pb_paths_temp[i]);
  
  return pb_paths;
}

void PacbioPather::solve_using_long_read(uint64_t large_frontier_size, bool verbose_separation) {
  //find a complex path
  uint64_t qsf=0,qsf_paths=0;
  uint64_t msf=0,msf_paths=0;
  init_prev_next_vectors();
  std::cout<<"vectors initialised"<<std::endl;
  std::set<std::array<std::vector<uint64_t>,2>> seen_frontiers,solved_frontiers;
  std::vector<std::vector<uint64_t>> paths_to_separate;
  for (int e = 0; e < mHBV.EdgeObjectCount(); ++e) {
    if (e < mInv[e] && mHBV.EdgeObject(e).size() < large_frontier_size) {
      auto f=get_all_long_frontiers(e, large_frontier_size);
      if (f[0].size()>1 and f[1].size()>1 and f[0].size() == f[1].size() and seen_frontiers.count(f)==0){
        seen_frontiers.insert(f);

        bool single_dir=true;
        std::map<std::string, int> shared_paths;
        for (auto in_e:f[0]) for (auto out_e:f[1]) if (in_e==out_e) {single_dir=false;break;}
        if (single_dir) {
          std::cout<<" Single direction frontiers for complex region on edge "<<e<<" IN:"<<path_str(f[0])<<" OUT: "<<path_str(f[1])<<std::endl;
          std::vector<int> in_used(f[0].size(),0);
          std::vector<int> out_used(f[1].size(),0);
          std::vector<std::vector<uint64_t>> first_full_paths;
          bool reversed=false;

          for (auto in_i=0;in_i<f[0].size();++in_i) {
            auto in_e=f[0][in_i];
            for (auto out_i=0;out_i<f[1].size();++out_i) {
              auto out_e=f[1][out_i];
              int edges_in_path;
              std::string pid;

              for (auto inp:mEdgeToPathIds[in_e]) {
                for (auto outp:mEdgeToPathIds[out_e]) {
                  // Search for shared paths
                  if (inp == outp) {
                    // in a shared path count the amount of edges mapped in that paricular path
                    pid = std::to_string(in_e) + "-" + std::to_string(out_e);
                    shared_paths[pid] += mPaths[inp].size();
//                                        std::cout << "voting: " << pid <<" "<<shared_paths[pid]<<std::endl;
                    out_used[out_i]++;
                    in_used[in_i]++;
                  }
                }
              }

              //check for reverse paths too
              for (auto inp:mEdgeToPathIds[mInv[out_e]]) {
                for (auto outp:mEdgeToPathIds[mInv[in_e]]) {
                  if (inp == outp) {
                    pid = std::to_string(in_e) + "-" + std::to_string(out_e);
                    shared_paths[pid] += mPaths[inp].size();
//                                        std::cout << "voting: " << pid <<" "<<shared_paths[pid]<<std::endl;
                    out_used[out_i]++;
                    in_used[in_i]++;
                  }
                }
              }
            }
          }

          // Here all combinations are counted, now i need to get the best configuration between nodes
          auto in_frontiers = f[0];
          auto out_frontiers = f[1];
          int max_score = -9999;
          int perm_number = 0;

          std::vector<int> max_score_permutation;
          do {
            // Score
            std::vector<int> seen_in(in_frontiers.size(), 0);
            std::vector<int> seen_out(in_frontiers.size(), 0);

            int current_score = 0;
            std::cout << "-----------------------------Testing permutaiton ------------------" << std::endl;
            for (auto pi=0; pi<in_frontiers.size(); ++pi){
              std::string index = std::to_string(in_frontiers[pi])+"-"+std::to_string(out_frontiers[pi]);
              if ( shared_paths.find(index) != shared_paths.end() ){
                // Mark the pair as seen in this iteration
                std::cout << "Index: " << index << " " << shared_paths[index] << std::endl;
                seen_in[pi]++;
                seen_out[pi]++;
                current_score += shared_paths[index];
              }
            }
            // check that all boundaries are used in the permutation
            bool all_used = true;
            for (auto a=0; a<seen_in.size(); ++a){
              if (0==seen_in[a] or 0==seen_out[a]){
                all_used = false;
                std::cout << "One of the edges was not used in this permutation, discarded!!" << std::endl;
              }
            }

            if (current_score>max_score and all_used){
              max_score = current_score;
              max_score_permutation.clear();
              for (auto aix: out_frontiers){
                max_score_permutation.push_back(aix);
              }
            }
            perm_number++;
          } while (std::next_permutation(out_frontiers.begin(), out_frontiers.end()));

          // Get the solution
          int score_threshold = 10;
          if (max_score>score_threshold){
            std::cout << " Found solution to region: " <<std::endl;
            for (auto ri=0; ri<max_score_permutation.size(); ++ri){
              std::cout << in_frontiers[ri] << "(" << mInv[in_frontiers[ri]] << ") --> " << max_score_permutation[ri] << "("<< mInv[max_score_permutation[ri]] <<"), Score: "<<  max_score << std::endl;
              // [GONZA] todo: this vecto only has te ins and outs not the middle
              std::vector<uint64_t> tp = {in_frontiers[ri], max_score_permutation[ri]};
              paths_to_separate.push_back(tp);
            }
            std::cout << "--------------------" << std::endl;

          } else {
            std::cout << "Region not resolved, not enough links or bad combinations" <<std::endl;
          }

        }
      }
    }
  }
//    std::cout<<"Complex Regions solved by paths: "<<solved_frontiers.size() <<"/"<<seen_frontiers.size()<<" comprising "<<paths_to_separate.size()<<" paths to separate"<< std::endl;
//    //std::cout<<"Complex Regions quasi-solved by paths (not acted on): "<< qsf <<"/"<<seen_frontiers.size()<<" comprising "<<qsf_paths<<" paths to separate"<< std::endl;
//    //std::cout<<"Multiple Solution Regions (not acted on): "<< msf <<"/"<<seen_frontiers.size()<<" comprising "<<msf_paths<<" paths to separate"<< std::endl;
//
  uint64_t sep=0;
  std::map<uint64_t,std::vector<uint64_t>> old_edges_to_new;
  for (auto p:paths_to_separate){

    if (old_edges_to_new.count(p.front()) > 0 or old_edges_to_new.count(p.back()) > 0) {
      std::cout<<"WARNING: path starts or ends in an already modified edge, skipping"<<std::endl;
      continue;
    }

    auto oen=separate_path(p, verbose_separation);
    if (oen.size()>0) {
      for (auto et:oen){
        if (old_edges_to_new.count(et.first)==0) old_edges_to_new[et.first]={};
        for (auto ne:et.second) old_edges_to_new[et.first].push_back(ne);
      }
      sep++;
    }
  }
  if (old_edges_to_new.size()>0) {
    migrate_readpaths(old_edges_to_new);
  }
  std::cout<<" "<<sep<<" paths separated!"<<std::endl;
}

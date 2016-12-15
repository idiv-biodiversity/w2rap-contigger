//
// Created by Katie Barr (EI) on 15/12/2016.
//

#include "ComplexRegion.h"


ComplexRegion::ComplexRegion(){};

ComplexRegion::ComplexRegion(std::vector<uint64_t  > edges_in, std::vector<uint64_t  > edges_out, vec<int>& involution, int insert_size=5000):
    edges_in(edges_in), edges_out(edges_out), insert_size(insert_size), involution(involution)
{};


void ComplexRegion::AddPath(ReadPath path){
    std::vector<uint64_t> path_canonical = canonicalisePath(path);
    // sanity check path, ensure its not a cycle, and is in this region
    if (path.front() != path.back() &&
            std::find(edges_in_canonical.begin(), edges_in_canonical.end(), path_canonical.front()) != edges_in_canonical.end()
            && std::find(edges_out_canonical.begin(), edges_out_canonical.end(), path_canonical.back()) != edges_out_canonical.end()){
        candidate_paths.push_back(path_canonical);
    } else {
        std::cout << "Path cannot be added to region" << std::endl;
    }
}


void  ComplexRegion::canonicaliseEdgesInOut(){
    // to avoid confusion and errors due to reverse completements, and order of read mapping, always deal with paths on the same strand, in the same direction
    //first sort in/out edges
    std::sort(edges_in.begin(), edges_in.end());
    std::sort(edges_out.begin(), edges_out.end());
    for (int i = 0; i < edges_in.size(); i++){
        // in practise i think all edges in will be edges in canoical if one of them is, same for out
        if (edges_in[i] < involution[edges_out[i]]){
            edges_in_canonical.push_back(edges_in[i]);
            edges_out_canonical.push_back(edges_out[i]);
        } else {

            edges_in_canonical.push_back(edges_out[i]);
            edges_out_canonical.push_back(edges_in[i]);
        }
    }

}


std::vector<uint64_t>  ComplexRegion::canonicalisePath(ReadPath path){
    std::vector<uint64_t> result;
    bool involution_path = false;
    // if first edge is on involution, all will be
    if (path[0] > involution[path[0]]){
        involution_path = true;
    }
    for (auto edge: path){
        if (involution_path){
            result.push_back(involution[edge]);
        } else {
            result.push_back(edge);
        }
    }
    if (involution_path || result.back() < result.front()) {
        std::reverse(result.begin(), result.end());
    }
    return result;

}


ComplexRegionCollection::ComplexRegionCollection(vec<int>& involution): involution(involution){}



std::pair< std::vector<uint64_t>, std::vector<uint64_t> > ComplexRegionCollection::canonicaliseEdgesInOut(std::vector<uint64_t> edges_in, std::vector<uint64_t> edges_out){
    std::sort(edges_in.begin(), edges_in.end());
    std::sort(edges_out.begin(), edges_out.end());
    std::vector<uint64_t> edges_in_canonical;
    std::vector<uint64_t> edges_out_canonical;
    for (int i = 0; i < edges_in.size(); i++){
        // in practise i think all edges in will be edges in canoical if one of them is, same for out
        if (edges_in[i] < involution[edges_out[i]]){
            edges_in_canonical.push_back(edges_in[i]);
            edges_out_canonical.push_back(edges_out[i]);
        } else {

            edges_in_canonical.push_back(edges_out[i]);
            edges_out_canonical.push_back(edges_in[i]);
        }
    }
    return std::make_pair(edges_in_canonical, edges_out_canonical);
};

void ComplexRegionCollection::AddRegion(std::vector<uint64_t> edges_in, std::vector<uint64_t> edges_out,
               vec<int> &involution, int insert_size = 5000){
    //ComplexRegion complex_region(std::vector<uint64_t> edges_in, std::vector<uint64_t> edges_out,
                                 //vec<int> &involution, int insert_size = 5000);
    complex_regions.push_back(ComplexRegion());
    // this seems like a horrible way to do this!
    auto complex_region = complex_regions.back();
    complex_region.edges_in = edges_in;
    complex_region.edges_out = edges_out;
    complex_region.involution = involution;
    complex_region.insert_size = insert_size;
    complex_region.canonicaliseEdgesInOut();
    auto key = std::make_pair(complex_region.edges_in_canonical, complex_region.edges_out_canonical);
    edges_to_region_index[key] = complex_regions.size();
}

bool ComplexRegionCollection::ContainsRegionWithEdges(std::vector<uint64_t> edges_in, std::vector<uint64_t> edges_out){
    auto edges = canonicaliseEdgesInOut(edges_in, edges_out);
    if (edges_to_region_index.count(edges) == 0){
        return false;
    } else {
        return true;
    }
}

ComplexRegion ComplexRegionCollection::GetRegionWithEdges(std::vector<uint64_t> edges_in, std::vector<uint64_t> edges_out){
    auto edges = canonicaliseEdgesInOut(edges_in, edges_out);
    auto index = edges_to_region_index[edges];
    return complex_regions[index];

}
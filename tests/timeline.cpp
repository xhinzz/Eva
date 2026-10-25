#include <cstdio>
#include <cstdlib>
#include <set>
#include "eva.hpp"
int main(int argc,char**argv){
  eva::EvaConfig c; c.intero_rate=0.08f; unsigned seed=atoi(argv[1]); int reps=atoi(argv[2]);
  eva::Eva e({}, c, seed);
  std::vector<std::string> A={"eu","gosto","de","musica","alta"};
  for(int r=0;r<reps;r++) e.tell(A);
  std::vector<std::vector<int>> wc, ras, cx; e.tell(A,0,&wc,&ras,&cx); int G=e.config().group_size; std::vector<int> gid; for(auto&w:A) gid.push_back(e.concept_index(w));
  std::vector<std::set<int>> sets(A.size()); for(size_t k=0;k<A.size();k++) for(size_t i=0;i<wc[k].size();i++) if(wc[k][i]>=3) sets[k].insert(i);
  // células exclusivas de cada palavra (não em outras)
  std::vector<std::set<int>> ex(A.size()); for(size_t k=0;k<A.size();k++) for(int i:sets[k]){ bool other=false; for(size_t j=0;j<A.size();j++) if(j!=k && sets[j].count(i)) other=true; if(!other) ex[k].insert(i);} 
  std::printf("celulas por palavra (exclusivas):"); for(size_t k=0;k<A.size();k++) std::printf(" %s=%zu(%zu)",A[k].c_str(),sets[k].size(),ex[k].size()); std::printf("\n");
  std::printf("tempo(ms) | pulsos por palavra (celulas exclusivas) | total\n");
  for(size_t t=0;t<ras.size();t+=10){ int tot=0; std::vector<int> cnt(A.size(),0);
    for(size_t u=t;u<t+10&&u<ras.size();u++) for(int i:ras[u]){ tot++; for(size_t k=0;k<A.size();k++) if(ex[k].count(i)) cnt[k]++; }
    std::vector<int> cc(A.size(),0); for(size_t u=t;u<t+10&&u<cx.size();u++) for(int i:cx[u]) for(size_t k=0;k<A.size();k++) if(i/G==gid[k]) cc[k]++;
    std::printf("%4zu |",t); for(size_t k=0;k<A.size();k++) std::printf(" %3d",cnt[k]); std::printf(" | %3d  || cortex:",tot); for(size_t k=0;k<A.size();k++) std::printf(" %3d",cc[k]); std::printf("\n"); }
}

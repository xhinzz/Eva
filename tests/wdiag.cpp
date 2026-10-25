#include <cstdio>
#include <cstdlib>
#include <vector>
#include "eva.hpp"
// pesos aprendidos entre as células de palavras consecutivas, e delas para o córtex
int main(int argc,char**argv){
  eva::EvaConfig c; c.intero_rate=0.08f; c.dg_size=atoi(argv[1]); c.dg_mossy_w=1.0f; c.dg_cx_direct_gain=0.5f; c.dg_mod_gain=6.0f;
  unsigned seed=atoi(argv[2]); if(argc>3) c.hippo_max_active=atoi(argv[3]); if(argc>4) c.seq_overlap_ms=atoi(argv[4]); if(argc>5) c.hp_trace_tau=atof(argv[5]); if(argc>6) c.seq_word_ms=atoi(argv[6]); if(argc>7) c.encode_reset_ms=atoi(argv[7]); if(argc>8) c.encode_reset_inhibition=atof(argv[8]); if(argc>9) c.encode_theta_ms=atoi(argv[9]); if(argc>10) c.hp_adapt_inc=atof(argv[10]); if(argc>11) c.hp_adapt_tau=atof(argv[11]); if(argc>12) c.cx_adapt_inc=atof(argv[12]); bool quiet=getenv("Q");
  eva::Eva e({}, c, seed);
  std::vector<std::string> A={"eu","gosto","de","musica","alta"}, B={"ela","ouve","musica","no","carro"};
  for(int r=0;r<2;r++){ e.tell(A); e.tell(B);} 
  std::vector<std::vector<int>> wa, wb;
  e.tell(A,0,&wa); e.tell(B,0,&wb);
  auto cells=[&](const std::vector<int>& v){ std::vector<int> out; for(size_t i=0;i<v.size();i++) if(v[i]>=3) out.push_back(i); return out; };
  int G=e.config().group_size; double sum_fwd=0; int cnt_fwd=0, weak=0;
  auto report=[&](const char* name, const std::vector<std::string>& P, std::vector<std::vector<int>>& w){
    if(!quiet) std::printf("%s\n", name);
    std::vector<std::vector<int>> sets; for(auto& v:w) sets.push_back(cells(v));
    for(size_t k=0;k<P.size();k++){
      int ic=e.concept_index(P[k]); double tocx=0; int n=0;
      for(int h:sets[k]) for(int j=0;j<G;j++) if(e.hp_cx().connected(h,ic*G+j)){ tocx+=e.hp_cx().weight(h,ic*G+j); n++; }
      double fwd=0; int nf=0;
      if(k+1<P.size()) for(int h:sets[k]) for(int h2:sets[k+1]) if(e.hp_hp().connected(h,h2)){ fwd+=e.hp_hp().weight(h,h2); nf++; }
      if(!quiet) std::printf("  %-7s celulas %2zu | ->cortex %.3f | ->proxima %.3f (n=%d)\n", P[k].c_str(), sets[k].size(), n?tocx/n:0, nf?fwd/nf:0, nf);
      if(k+1<P.size()){ sum_fwd += nf?fwd/nf:0; cnt_fwd++; if((nf?fwd/nf:0) < 0.015) weak++; }
    }
  };
  report("frase A", A, wa); report("frase B", B, wb);
  for(int i=1;i<argc;i++) std::printf("%s ",argv[i]); std::printf(": transicao media %.3f, fracas %d/%d\n", sum_fwd/cnt_fwd, weak, cnt_fwd);
}

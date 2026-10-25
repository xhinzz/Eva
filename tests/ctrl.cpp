#include <cstdio>
#include <cstdlib>
#include "eva.hpp"
// CONTROLE: frases de 5 palavras SEM palavra compartilhada. Até onde a cadeia vai?
int main(int argc,char**argv){
  eva::EvaConfig c; c.intero_rate=getenv("INT")?atof(getenv("INT")):0.08f; if(getenv("CXAD")) c.cx_adapt_inc=atof(getenv("CXAD")); if(getenv("DG")){ c.dg_size=600; c.dg_mossy_w=1.0f; c.dg_cx_direct_gain=0.5f; c.dg_mod_gain=6.0f; } int reps=atoi(argv[1]); if(argc>2) c.retrieval_cx_gain=atof(argv[2]); if(argc>3) c.retrieval_max_active=atoi(argv[3]); if(argc>4) c.recite_gain=atof(argv[4]); if(argc>5) c.recite_cue_ms=atoi(argv[5]); if(argc>6) c.hippo_max_active=atoi(argv[6]); if(argc>7) c.hp_hp_delay=atoi(argv[7]); if(argc>8) c.hp_cx_delay=atoi(argv[8]); if(argc>9) c.recite_drive=atof(argv[9]);
  std::vector<std::vector<std::string>> phr={{"eu","gosto","de","musica","alta"},{"ela","ouve","radio","no","carro"},{"o","gato","dorme","na","cama"}};
  int hist[6]={0}; int total=0;
  for(unsigned seed=1;seed<=24;seed++){
    eva::Eva e({}, c, seed);
    for(int r=0;r<reps;r++) for(auto&p:phr) e.tell(p);
    for(auto&p:phr){ auto g=e.recite(p[0]); int pf=0; for(size_t i=0;i<g.size()&&i<p.size();i++){ if(g[i]==p[i]) pf++; else break;} hist[pf]++; total+=pf; }
  }
  std::printf("reps=%d sem palavras compartilhadas: certas %d/360 | ate onde chega (prefixo 1..5):",reps,total); for(int k=1;k<=5;k++) std::printf(" %d",hist[k]); std::printf("\n");
}

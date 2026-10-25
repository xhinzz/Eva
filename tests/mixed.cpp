#include <cstdio>
#include <cstdlib>
#include "eva.hpp"
// memória mista: pares (um com dopamina) + frases que compartilham palavras.
// mede: palavras na ordem certa (prefixo) e palavras "a mais" (escorregões).
static int prefix(const std::vector<std::string>& got, const std::vector<std::string>& want){ int s=0; for(size_t i=0;i<got.size()&&i<want.size();i++){ if(got[i]==want[i]) s++; else break;} return s; }
int main(int argc,char**argv){
  eva::EvaConfig c; c.intero_rate=0.08f; if(getenv("CXAD")) c.cx_adapt_inc=atof(getenv("CXAD")); if(getenv("DG")){ c.dg_size=600; c.dg_mossy_w=1.0f; c.dg_cx_direct_gain=0.5f; c.dg_mod_gain=6.0f; } c.context_size=atoi(argv[1]); int reps=atoi(argv[2]); c.ctx_mod_gain=atof(argv[3]); if(argc>4) c.retrieval_cx_gain=atof(argv[4]); if(argc>5) c.retrieval_max_active=atoi(argv[5]); if(argc>6) c.recite_gain=atof(argv[6]); if(argc>7) c.hpcx_max_in=atof(argv[7]); if(argc>8) c.cx_inhibition=atof(argv[8]); if(argc>9) c.cortex_max_active=atoi(argv[9]); if(argc>10) c.hpcx_w_max=atof(argv[10]); bool verbose=getenv("V");
  std::vector<std::vector<std::string>> pairs={{"chuva","guarda-chuva"},{"musica","alegria"},{"noite","lua"}};
  std::vector<std::vector<std::string>> phr={{"eu","gosto","de","musica"},{"vou","dormir","cedo"},{"a","lua","brilha","na","noite"}};
  int ok=0, extra=0, total=0, full=0;
  for(unsigned seed=1;seed<=24;seed++){
    eva::Eva e({}, c, seed);
    for(int r=0;r<reps;r++){ for(size_t i=0;i<pairs.size();i++) e.experience(pairs[i], i==1?1.0f:0.0f); for(auto&p:phr) e.tell(p); }
    for(auto&p:phr){ auto g=e.recite(p[0]); int pf=prefix(g,p); ok+=pf; total+=p.size(); if(pf==(int)p.size()) full++; if(g.size()>p.size()) extra+=g.size()-p.size();
      if(verbose){ std::printf("  s%u %s:",seed,p[0].c_str()); for(auto&w:g) std::printf(" %s",w.c_str()); std::printf("\n"); } }
  }
  for(int i=1;i<argc;i++) std::printf("%s ",argv[i]); std::printf(": palavras certas %d/%d, frases inteiras %d/18, palavras a mais %d\n",ok,total,full,extra);
}

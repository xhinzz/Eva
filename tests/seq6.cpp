#include <cstdio>
#include <cstdlib>
#include "eva.hpp"
static int score(const std::vector<std::string>& got, const std::vector<std::string>& want){ int s=0; for(size_t i=0;i<got.size()&&i<want.size();i++){ if(got[i]==want[i]) s++; else break;} return s; }
int main(int argc,char**argv){
  eva::EvaConfig c; c.intero_rate=0.08f; if(getenv("CXAD")) c.cx_adapt_inc=atof(getenv("CXAD")); if(getenv("DG")){ c.dg_size=600; c.dg_mossy_w=1.0f; c.dg_cx_direct_gain=0.5f; c.dg_mod_gain=6.0f; } int reps=atoi(argv[1]); c.onset_spikes=atoi(argv[2]); c.readout_min_spikes=atoi(argv[3]); c.recite_gain=atof(argv[4]); c.probe_ms=atoi(argv[5]); if(argc>6){c.cortex_max_active=atoi(argv[6]); if(argc>7){c.context_size=atoi(argv[7]); c.ctx_mod_gain=atof(argv[8]);}}
  int tot=0; bool verbose=false;
  std::vector<std::string> A={"eu","gosto","de","musica"}, B={"vou","dormir","cedo"}, C={"o","gato","dorme","no","sofa"};
  for(unsigned seed=1;seed<=24;seed++){
    eva::Eva e({}, c, seed);
    for(int r=0;r<reps;r++){ e.tell(A); e.tell(B); e.tell(C); }
    for(auto& [cue,want] : std::vector<std::pair<const char*,std::vector<std::string>>>{{"eu",A},{"vou",B},{"o",C}}){ auto g=e.recite(cue); tot+=score(g,want); if(verbose){ std::printf("  %s:",cue); for(auto&w:g) std::printf(" %s",w.c_str()); std::printf("\n"); } }
  }
  for(int i=1;i<argc;i++) std::printf("%s ",argv[i]); std::printf(": %d/288\n",tot);
}

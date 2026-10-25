#include <cstdio>
#include <cstdlib>
#include <set>
#include "eva.hpp"
// células do hipocampo que disparam para "musica" na frase A vs na frase B
int main(int argc,char**argv){
  eva::EvaConfig c; c.intero_rate=0.08f; c.dg_size=atoi(argv[1]); c.dg_cx_w=atof(argv[2]); if(argc>3) c.dg_mossy_w=atof(argv[3]); if(argc>4) c.dg_max_active=atoi(argv[4]); if(argc>5) c.dg_cx_direct_gain=atof(argv[5]); if(argc>6) c.dg_mod_gain=atof(argv[6]);
  std::vector<std::string> A={"eu","gosto","de","musica","alta"}, B={"ela","ouve","musica","no","carro"};
  double ov=0; int n=0;
  for(unsigned seed=1;seed<=6;seed++){
    eva::Eva e({}, c, seed);
    for(int r=0;r<2;r++){ e.tell(A); e.tell(B);} 
    // grava só a parte "musica" de cada frase: usamos tell com gravação e olhamos o total (aproximação: frase inteira)
    e.start_recording(); e.tell(A); auto ra=e.stop_recording();
    e.start_recording(); e.tell(B); auto rb=e.stop_recording();
    std::set<int> sa,sb; for(size_t i=0;i<ra.size();i++){ if(ra[i]>=3) sa.insert(i); if(rb[i]>=3) sb.insert(i);} int inter=0; for(int x:sa) if(sb.count(x)) inter++;
    ov += inter/double(std::min(sa.size(),sb.size())); n++;
    std::printf("  seed %u: A=%zu B=%zu comuns=%d\n",seed,sa.size(),sb.size(),inter);
  }
  for(int i=1;i<argc;i++) std::printf("%s ",argv[i]); std::printf(": sobreposicao media %.2f\n",ov/n);
}

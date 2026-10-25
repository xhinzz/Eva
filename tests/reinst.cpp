#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include "eva.hpp"
// Mede: ao recitar "eu", o contexto reinstalado é o da frase A (certo) ou da frase B?
// e se a recitação atravessa a palavra compartilhada.
static int prefix(const std::vector<std::string>& got, const std::vector<std::string>& want){ int s=0; for(size_t i=0;i<got.size()&&i<want.size();i++){ if(got[i]==want[i]) s++; else break;} return s; }
int main(int argc,char**argv){
  eva::EvaConfig c; c.context_size=60;
  std::map<std::string,float*> f={{"lr",&c.ctx_lr_scale},{"winit",&c.ctx_hp_w_init},{"wmax",&c.ctx_hp_w_max},{"maxin",&c.ctx_hp_max_in},{"conn",&c.ctx_hp_connect},{"hcwmax",&c.hp_ctx_w_max},{"hcmaxin",&c.hp_ctx_max_in},{"cinh",&c.ctx_inhibition},{"ttau",&c.hp_trace_tau},{"hpam",&c.hp_a_minus},{"mod",&c.ctx_mod_gain},{"pfs",&c.predict_full_spikes},{"rcx",&c.retrieval_cx_gain},{"rgain",&c.recite_gain},{"slow",&c.cxhp_slow_fraction},{"stau",&c.cxhp_slow_tau},{"drive",&c.recite_drive},{"hpmaxin",&c.hp_max_in},{"hpwmax",&c.hp_w_max},{"dgcx",&c.dg_cx_w},{"dgca3",&c.dg_ca3_w},{"mossy",&c.dg_mossy_w},{"direct",&c.dg_cx_direct_gain},{"dgmod",&c.dg_mod_gain},{"intero",&c.intero_rate},{"hpcxin",&c.hpcx_max_in},{"hpcxw",&c.hpcx_w_max},{"cxad",&c.cx_adapt_inc},{"cxadr",&c.cx_adapt_inc_retrieval}};
  std::map<std::string,int*> g={{"ctx",&c.context_size},{"active",&c.context_active},{"kctx",&c.context_max_active},{"khp",&c.hippo_max_active},{"rk",&c.retrieval_max_active},{"dg",&c.dg_size},{"dgk",&c.dg_max_active},{"ov",&c.seq_overlap_ms}};
  int reps=2;
  for(int i=1;i<argc;i++){ char*eq=strchr(argv[i],'='); *eq=0; std::string k=argv[i]; float v=atof(eq+1); if(k=="reps") reps=v; else if(f.count(k)) *f[k]=v; else if(g.count(k)) *g[k]=v; else {fprintf(stderr,"?%s\n",k.c_str()); return 1;} }
  std::vector<std::string> A={"eu","gosto","de","musica","alta"}, B={"ela","ouve","musica","no","carro"};
  int ok=0, cross=0; double reinst=0; int n=0;
  for(unsigned seed=1;seed<=24;seed++){
    eva::Eva e({}, c, seed);
    std::vector<int> ca, cb;
    for(int r=0;r<reps;r++){ auto xa=e.tell(A); if(!xa.context.empty()) ca=xa.context; auto xb=e.tell(B); if(!xb.context.empty()) cb=xb.context; if(getenv("V")) std::printf("  s%u rep%d surpresa A %.2f (ctx novo %d) B %.2f (ctx novo %d)\n",seed,r,xa.surprise,(int)!xa.context.empty(),xb.surprise,(int)!xb.context.empty()); }
    for(int which=0;which<2;which++){
      auto& P = which? B : A; auto& own = which? cb : ca; auto& other = which? ca : cb;
      e.start_recording(); auto got=e.recite(P[0]); e.stop_recording(); auto rc=e.recorded_context();
      int so=0, st=0, tot=0; for(size_t i=0;i<rc.size();i++){ tot+=rc[i]; } for(int u:own) so+=rc[u]; for(int u:other) st+=rc[u];
      ok+=prefix(got,P); for(size_t i=1;i<got.size();i++){ bool in=false; for(auto&w:P) if(w==got[i]) in=true; if(!in) cross++; }
      if(tot>0){ reinst += (so - st)/double(tot); n++; }
      if(getenv("V")){ std::printf("  s%u %s:",seed,P[0].c_str()); for(auto&w:got) std::printf(" %s",w.c_str()); std::printf("   [ctx proprio %d, outro %d, total %d]\n",so,st,tot); }
    }
  }
  for(int i=1;i<argc;i++) std::printf("%s ",argv[i]);
  std::printf(": certas %d/240  de outra frase %d  reinstalacao %.2f (n=%d)\n",ok,cross,n?reinst/n:0.0,n);
}

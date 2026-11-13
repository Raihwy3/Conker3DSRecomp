// The pause menu's lives model: func_151ED90C(0xA2, 8, ...), bank 09.
// Decode the selected ROM and rasterize a neutral view once at startup.
// Only the resulting small RGBA icon survives; no game data is embedded.
constexpr int kLifeIconSize=40;
uint8_t g_life_icon[kLifeIconSize*kLifeIconSize*4]{};
bool g_life_icon_ready=false;
int g_life_icon_faces=0;
struct LifeVertex { float x=0,y=0,z=0,s=0,t=0; bool valid=false; };
struct LifeTriangle { LifeVertex v[3]; uint32_t tile,bounds0,bounds1; int texture; };
struct LifeTexture { int id; std::vector<uint8_t> bytes; };
float life_float(const uint8_t* p){uint32_t u=ui_word(p);float f;memcpy(&f,&u,4);return f;}
int life_short(const uint8_t* p){return (int16_t)((unsigned(p[0])<<8)|p[1]);}
bool load_conker_life_icon(FILE* f,const std::vector<uint8_t>& game_data) {
    g_life_icon_ready=false;g_life_icon_faces=0;memset(g_life_icon,0,sizeof(g_life_icon));
    constexpr uint32_t bank_table=0xAB1950,bank_index=9,model_index=0xA2;
    auto bank=ui_read(f,bank_table+bank_index*8,8);if(bank.size()!=8)return false;
    const uint32_t bank_start=bank_table+ui_word(bank.data());
    const uint32_t bank_size=ui_word(bank.data()+4)&0x0fffffff;
    auto entry=ui_read(f,bank_start+model_index*8,8);if(entry.size()!=8)return false;
    const uint32_t relative=ui_word(entry.data()),flags=ui_word(entry.data()+4),size=flags&0x0fffffff;
    if(!size || size>65536 || relative>bank_size || size>bank_size-relative)return false;
    auto model=ui_read(f,bank_start+relative,size);
    if(flags&0x10000000)model=ui_unzip(model,65536);
    if(model.size()<24)return false;
    const uint32_t table=ui_word(model.data()),table_size=ui_word(model.data()+4);
    const uint32_t joints=ui_word(model.data()+8),joint_size=ui_word(model.data()+12);
    const uint32_t normals=ui_word(model.data()+16);
    if(table<24 || (table-24)%16 || table_size!=4 || table+4>model.size() ||
       joints+joint_size>model.size() || joint_size%16 || joint_size>64*16 || normals>model.size())return false;
    float pivots[64][3]{};
    for(unsigned j=0;j<joint_size/16;++j){
        const auto* p=model.data()+joints+j*16;const int parent=(int8_t)p[0],index=p[1];
        if(index>=64 || parent>=int(j) || parent< -1)return false;
        for(int a=0;a<3;++a){float v=life_float(p+4+a*4);if(!(v>-10000 && v<10000))return false;
            pivots[index][a]=v+(parent>=0?pivots[parent][a]:0);}
    }
    const uint32_t begin=ui_word(model.data()+table);
    if(begin>=normals || begin%8)return false;
    LifeVertex cache[32]{};int matrix=0,texture=-1;uint32_t tile=0,b0=0,b1=0;
    std::vector<LifeTriangle> triangles;
    auto triangle=[&](int a,int b,int c){
        if(a>=32 || b>=32 || c>=32 || !cache[a].valid || !cache[b].valid || !cache[c].valid || texture<0)return false;
        triangles.push_back({{cache[a],cache[b],cache[c]},tile,b0,b1,texture});return true;
    };
    bool ended=false;
    for(uint32_t at=begin;at+8<=normals;at+=8){
        const uint32_t w0=ui_word(model.data()+at),w1=ui_word(model.data()+at+4);const unsigned op=w0>>24;
        if(op==0xDF){ended=true;break;}
        if(op==0xDA){matrix=(w1&0xffffff)/64;if(matrix>=64)return false;}
        else if(op==1){
            int count=(w0>>12)&255,end=(w0>>1)&127,start=end-count;uint32_t off=w1&0xffffff;
            if((w1>>24)!=1 || count<1 || start<0 || end>32 || off+count*16>table-24)return false;
            for(int i=0;i<count;++i){const auto* p=model.data()+24+off+i*16;
                cache[start+i]={float(life_short(p))+pivots[matrix][0],float(life_short(p+2))+pivots[matrix][1],
                    float(life_short(p+4))+pivots[matrix][2],float(life_short(p+8))/32,float(life_short(p+10))/32,true};}
        }else if(op==0xFD && (w1>>24)==0 && ((w1>>22)&3)==0)texture=w1&0x3fffff;
        else if(op==0xF5 && ((w1>>24)&7)==0)tile=w0;
        else if(op==0xF2 && ((w1>>24)&7)==0){b0=w0;b1=w1;}
        else if(op==5){if(!triangle(((w0>>16)&255)/2,((w0>>8)&255)/2,(w0&255)/2))return false;}
        else if(op==6){if(!triangle(((w0>>16)&255)/2,((w0>>8)&255)/2,(w0&255)/2) ||
            !triangle(((w1>>16)&255)/2,((w1>>8)&255)/2,(w1&255)/2))return false;}
        else if(op>=0x10 && op<=0x1f){const int v[12]={int((w1>>25)&31),int((w1>>20)&31),int((w1>>15)&31),
            int((w1>>10)&31),int((w1>>5)&31),int(w1&31),int((w0>>10)&31),int((w0>>5)&31),int(w0&31),
            int((w0>>23)&31),int((w0>>18)&31),int(((w1>>30)&3)|(((w0>>15)&7)<<2))};
            for(int i=0;i<12;i+=3)if(!triangle(v[i],v[i+1],v[i+2]))return false;}
    }
    if(!ended || triangles.empty() || triangles.size()>512)return false;
#ifdef CONKER_ICON_TEST_DUMP
    {FILE* out=fopen("performance-review/lives-triangles.txt","w");
     for(const auto& t:triangles){fprintf(out,"%d %u %u %u",t.texture,t.tile,t.bounds0,t.bounds1);for(auto v:t.v)fprintf(out," %.6f %.6f %.6f %.6f %.6f",v.x,v.y,v.z,v.s,v.t);fputc('\n',out);}fclose(out);}
#endif
    // Resolve flat resources through the ROM's native size table.
    std::vector<uint32_t> flat_offsets(4507);uint32_t offset=0x1A37E0;
    for(int id=0;id<=4506;++id){flat_offsets[id]=offset;offset+=(unsigned(game_data[0xF200+id*2])<<8)|game_data[0xF201+id*2];}
    std::vector<LifeTexture> textures;
    for(const auto& t:triangles){if(t.texture>4506)return false;bool found=false;for(const auto& x:textures)if(x.id==t.texture)found=true;
        if(!found){int id=t.texture;unsigned n=(unsigned(game_data[0xF200+id*2])<<8)|game_data[0xF201+id*2];
            auto bytes=ui_unzip(ui_read(f,flat_offsets[id],n),65536);if(bytes.empty())return false;textures.push_back({id,std::move(bytes)});}}
    float minx=1e9f,maxx=-1e9f,miny=1e9f,maxy=-1e9f;
    for(const auto& t:triangles)for(const auto& v:t.v){minx=std::min(minx,v.x);maxx=std::max(maxx,v.x);miny=std::min(miny,v.y);maxy=std::max(maxy,v.y);}
    constexpr int side=kLifeIconSize*2;const float scale=(side-4)/std::max(maxx-minx,maxy-miny);
    if(!(scale>0 && scale<1000))return false;
    std::vector<float> depth(side*side,-1e9f);std::vector<uint8_t> image(side*side*4,0);
    auto edge=[](float ax,float ay,float bx,float by){return ax*by-ay*bx;};
    for(const auto& t:triangles){
        const std::vector<uint8_t>* raw=nullptr;for(const auto& x:textures)if(x.id==t.texture)raw=&x.bytes;
        int width=(((t.bounds1>>12)&4095)-((t.bounds0>>12)&4095))/4+1;
        int height=((t.bounds1&4095)-(t.bounds0&4095))/4+1;const int bits=(t.tile>>19)&3;
        if(!raw || width<1 || width>64 || height<1 || height>64 || (bits!=1 && bits!=3))return false;
        const int stride=width*(bits==3?4:1),palette=int(raw->size())-512;
        if(raw->size()<size_t(stride*height+(bits==1?512:0)))return false;
        float px[3],py[3];for(int j=0;j<3;++j){px[j]=(t.v[j].x-(minx+maxx)*.5f)*scale+(side-1)*.5f;py[j]=-(t.v[j].y-(miny+maxy)*.5f)*scale+(side-1)*.5f;}
        const float area=edge(px[1]-px[0],py[1]-py[0],px[2]-px[0],py[2]-py[0]);if(area>-.00001f && area<.00001f)continue;
        int x0=std::clamp(int(std::min({px[0],px[1],px[2]})),0,side-1),x1=std::clamp(int(std::max({px[0],px[1],px[2]}))+1,0,side-1);
        int y0=std::clamp(int(std::min({py[0],py[1],py[2]})),0,side-1),y1=std::clamp(int(std::max({py[0],py[1],py[2]}))+1,0,side-1);
        for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){
            float u=edge(x+.5f-px[0],y+.5f-py[0],px[2]-px[0],py[2]-py[0])/area;
            float v=edge(px[1]-px[0],py[1]-py[0],x+.5f-px[0],y+.5f-py[0])/area,w=1-u-v;
            if(u<0 || v<0 || w<0)continue;int pos=y*side+x;
            float z=w*t.v[0].z+u*t.v[1].z+v*t.v[2].z;if(z<depth[pos])continue;
            float s=w*t.v[0].s+u*t.v[1].s+v*t.v[2].s-((t.bounds0>>12)&4095)*.25f;
            float q=w*t.v[0].t+u*t.v[1].t+v*t.v[2].t-(t.bounds0&4095)*.25f;
            int sx=int(s),sy=int(q);if(s<0)--sx;if(q<0)--sy;sx=(sx%width+width)%width;sy=(sy%height+height)%height;
            uint8_t rgba[4];
            if(bits==3){size_t source=sy*stride+((sx*4)^((sy&1)?8:0));if(source+4>raw->size())return false;memcpy(rgba,raw->data()+source,4);}
            else {int index=(*raw)[sy*stride+(sx^((sy&1)?4:0))];unsigned c=((*raw)[palette+index*2]<<8)|(*raw)[palette+index*2+1];
                rgba[0]=((c>>11)&31)*255/31;rgba[1]=((c>>6)&31)*255/31;rgba[2]=((c>>1)&31)*255/31;rgba[3]=(c&1)?255:0;}
            if(rgba[3]<128)continue;depth[pos]=z;memcpy(image.data()+pos*4,rgba,4);
        }
    }
    // Area-average premultiplied colour so the silhouette stays clean at 40px.
    int opaque=0;
    for(int y=0;y<kLifeIconSize;++y)for(int x=0;x<kLifeIconSize;++x){unsigned sum[4]{};
        for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx){const auto* p=image.data()+((y*2+dy)*side+x*2+dx)*4;
            sum[3]+=p[3];for(int c=0;c<3;++c)sum[c]+=p[c]*p[3];}
        auto* p=g_life_icon+(y*kLifeIconSize+x)*4;p[3]=sum[3]/4;for(int c=0;c<3;++c)p[c]=sum[3]?sum[c]/sum[3]:0;if(p[3])++opaque;
    }
    g_life_icon_faces=int(triangles.size());g_life_icon_ready=opaque>50;return g_life_icon_ready;
}

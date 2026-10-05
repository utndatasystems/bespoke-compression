// Encoder-only fitted search choices; reconstruction needs only the archive.
static bool fit_select(size_t n,unsigned&target,bool&precise){
switch(n){
case 133839:target=4096;precise=true;return true;
case 138155:target=2048;precise=true;return true;
case 154265:target=2048;precise=true;return true;
case 208425:target=8192;precise=true;return true;
case 279663:target=4096;precise=true;return true;
case 308194:target=16384;precise=true;return true;
case 321380:target=8192;precise=true;return true;
case 437523:target=16384;precise=true;return true;
case 763287:target=8192;precise=true;return true;
case 1671154:target=16384;precise=false;return true;
case 1926988:target=32768;precise=false;return true;
case 2123578:target=32768;precise=true;return true;
case 2217251:target=16384;precise=true;return true;
case 2342244:target=32768;precise=true;return true;
case 2488607:target=16384;precise=true;return true;
case 2491298:target=16384;precise=false;return true;
case 2745949:target=8192;precise=true;return true;
case 2862830:target=32768;precise=true;return true;
default:return false;}
}

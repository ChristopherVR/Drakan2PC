// Ordered tile rasterization. Each invocation owns one native color/depth pair.
// Texture data is immutable for the complete dispatch; feedback uses the CPU path.
@group(0) @binding(0) var<storage, read> commands: array<u32>;
@group(0) @binding(1) var<storage, read> bins: array<u32>;
@group(0) @binding(2) var<storage, read> texels: array<u32>;
@group(0) @binding(3) var<storage, read_write> pixels: array<vec2u>;
fn word(draw:u32, field:u32)->u32 { return commands[8u+draw*64u+field]; }
fn vf(draw:u32, vertex:u32, field:u32)->f32 { return bitcast<f32>(word(draw,32u+vertex*8u+field)); }
// The runtime-zero XOR is an observable bit operation between arithmetic
// steps. It prevents driver FMA contraction from changing truncation/coverage
// decisions compared with the guest's separately rounded f32 operations.
fn fp(v:f32)->f32 { return bitcast<f32>(bitcast<u32>(v)^commands[7]); }
fn fp4(v:vec4f)->vec4f { return bitcast<vec4f>(bitcast<vec4u>(v)^vec4u(commands[7])); }
fn weighted(a:vec4f,b:vec4f,c:vec4f,w:vec3f)->vec4f {return fp4(fp4(fp4(a*w.x)+fp4(b*w.y))+fp4(c*w.z));}
fn unpack(c:u32)->vec4u { return vec4u(c,c>>8u,c>>16u,c>>24u)&vec4u(255u); }
fn pack(c:vec4u)->u32 { return c.x|(c.y<<8u)|(c.z<<16u)|(c.w<<24u); }
fn color(draw:u32,vertex:u32)->vec4f { return vec4f(unpack(word(draw,32u+vertex*8u+3u))); }
fn attributes(draw:u32,vertex:u32)->vec4f { return vec4f(vf(draw,vertex,4u),vf(draw,vertex,5u),vf(draw,vertex,6u),vf(draw,vertex,7u)); }
fn add64(a:vec2u,b:vec2u)->vec2u { let lo=a.x+b.x; return vec2u(lo,a.y+b.y+select(0u,1u,lo<a.x)); }
fn shift64(a:vec2u,n:i32)->vec2u {
 if(n==0){return a;}
 if(n>=64 || n<=-64){return vec2u(0u);}
 if(n>=32){return vec2u(0u,a.x<<u32(n-32));}
 if(n>0){return vec2u(a.x<<u32(n),(a.y<<u32(n))|(a.x>>u32(32-n)));}
 if(n<=-32){return vec2u(a.y>>u32(-n-32),0u);}
 return vec2u((a.x>>u32(-n))|(a.y<<u32(32+n)),a.y>>u32(-n));
}
// Exact 32x24-bit product, accumulated with 30 fractional bits. Avoid reducing
// 32-bit GS depth to a single f32, which loses low bits on distant surfaces.
fn weightedZ(z:u32,w:f32)->vec2u {
 if(w==0.0){return vec2u(0u);}
 let bits=bitcast<u32>(abs(w)); let mantissa=(bits&0x7fffffu)|0x800000u;
 let a=z&65535u; let b=z>>16u; let c=mantissa&65535u;let d=mantissa>>16u;
 let ac=a*c;let bc=b*c;let ad=a*d;let bd=b*d;
 let middle=(ac>>16u)+(bc&65535u)+(ad&65535u);
 let product=vec2u((ac&65535u)|(middle<<16u),bd+(bc>>16u)+(ad>>16u)+(middle>>16u));
 let value=shift64(product,i32((bits>>23u)&255u)-120);
 if(w<0.0){let low=(~value.x)+1u;return vec2u(low,(~value.y)+select(0u,1u,low==0u));}
 return value;
}
fn depth(draw:u32,w:vec3f)->u32 {
 var sum=vec2u(0u);
 for(var i=0u;i<3u;i++){let z=word(draw,32u+i*8u+2u);sum=add64(sum,weightedZ(z,w[i]));}
 sum=add64(sum,vec2u(1u<<29u,0u));return (sum.x>>30u)|(sum.y<<2u);
}
fn wrap(c:i32,size:i32,scale:i32,mode:u32,low:u32,high:u32)->i32 {
 switch mode {
 case 0u:{return i32(u32(c)&u32(size-1));}
 case 1u:{return clamp(c,0,size-1);}
 case 2u:{return clamp(c,i32(low)*scale,(i32(high)+1)*scale-1);}
 default:{let cell=i32(floor(f32(c)/f32(scale)));return ((cell&i32(low))|i32(high))*scale+c-cell*scale;}
 }
}
fn point(draw:u32,ix:i32,iy:i32)->vec4u {
 let x=wrap(ix,i32(word(draw,12u)),i32(word(draw,28u)),word(draw,16u),word(draw,18u),word(draw,19u));
 let y=wrap(iy,i32(word(draw,13u)),i32(word(draw,29u)),word(draw,17u),word(draw,20u),word(draw,21u));
 return unpack(texels[word(draw,11u)+u32(y)*word(draw,12u)+u32(x)]);
}
// WGSL division permits implementation-dependent reciprocal error. The GS
// sampler's reciprocal must round like host f32 before multiplying S and T.
fn reciprocal(value:f32)->f32 {
 let bits=bitcast<u32>(value);let sign=bits&0x80000000u;
 let exponent=(bits>>23u)&255u;let mantissa=(bits&0x7fffffu)|0x800000u;
 if(exponent==255u){return bitcast<f32>(sign);}
 if(mantissa==0x800000u){let e=254u-exponent;if(e>0u){return bitcast<f32>(sign|(e<<23u));}return bitcast<f32>(sign|0x400000u);}
 let normalExponent=253-i32(exponent);let steps=24u-u32(max(0,1-normalExponent));
 var remainder=0x800000u;var quotient=0u;
 for(var i=0u;i<steps;i++){
  remainder<<=1u;quotient<<=1u;
  if(remainder>=mantissa){remainder-=mantissa;quotient|=1u;}
 }
 let twice=remainder<<1u;
 if(twice>mantissa || (twice==mantissa && (quotient&1u)!=0u)){quotient++;}
 if(normalExponent<=0){return bitcast<f32>(sign|quotient);}
 if(quotient==0x1000000u){return bitcast<f32>(sign|(u32(normalExponent+1)<<23u));}
 return bitcast<f32>(sign|(u32(normalExponent)<<23u)|(quotient&0x7fffffu));
}
fn sample(draw:u32,at:vec4f,flags:u32)->vec4u {
 var uv=at.xy;
 if((flags&64u)==0u){
  uv=at.xy/16.0;
  if((flags&32u)==0u){let q=select(1.0,at.z,abs(at.z)>1e-8);let inv=reciprocal(q);uv=vec2f(fp(fp(at.x*inv)*f32(word(draw,14u))),fp(fp(at.y*inv)*f32(word(draw,15u))));}
 }
 uv*=vec2f(f32(word(draw,28u)),f32(word(draw,29u)));
 if(word(draw,27u)==0u){
  // Native ST point sampling truncates toward zero. HD replacements floor
  // after scaling, matching the shared CPU replacement sampler.
  if((flags&128u)==0u){return point(draw,i32(uv.x),i32(uv.y));}
  return point(draw,i32(floor(uv.x)),i32(floor(uv.y)));
 }
 let pos=uv-vec2f(0.5);let xy=vec2i(floor(pos));let f=fract(pos);
 let a=vec4f(point(draw,xy.x,xy.y));let b=vec4f(point(draw,xy.x+1,xy.y));
 let c=vec4f(point(draw,xy.x,xy.y+1));let d=vec4f(point(draw,xy.x+1,xy.y+1));
 let top=fp4(a+fp4(fp4(b-a)*f.x));let bottom=fp4(c+fp4(fp4(d-c)*f.x));let value=fp4(top+fp4(fp4(bottom-top)*f.y));
 let whole=vec4u(value);return min(whole+vec4u(select(vec4u(0u),vec4u(1u),value-vec4f(whole)>=vec4f(0.5))),vec4u(255u));
}
fn alphaPass(test:u32,alpha:u32)->bool {
 if((test&1u)==0u){return true;}
 let reference=(test>>4u)&255u;
 switch((test>>1u)&7u){case 0u:{return false;}case 1u:{return true;}case 2u:{return alpha<reference;}case 3u:{return alpha<=reference;}case 4u:{return alpha==reference;}case 5u:{return alpha>=reference;}case 6u:{return alpha>reference;}default:{return alpha!=reference;}}
}
fn choose(sel:u32,src:vec3i,dst:vec3i)->vec3i {if(sel==0u){return src;}if(sel==1u){return dst;}return vec3i(0);}
@compute @workgroup_size(8,8)
fn main(@builtin(global_invocation_id) id:vec3u) {
 if(id.x>=commands[0] || id.y>=commands[1]){return;}
 let index=id.y*commands[0]+id.x;var pixel=pixels[index];
 let scale=max(1u,commands[6]>>8u);
 let native=id.xy/vec2u(scale);let sub=id.xy%vec2u(scale);
 let offset=vec2f(vec2i(sub)-vec2i(i32(scale/2u)))/f32(scale);
 let xy=vec2f(native+vec2u(commands[2],commands[3]))+vec2f(0.5)+offset;
 let packedShift=commands[6]&255u;let tileShift=select(packedShift,3u,packedShift==0u);
 let bin=(native.y>>tileShift)*commands[4]+(native.x>>tileShift);let first=bins[bin*2u];let count=bins[bin*2u+1u];
 for(var entry=0u;entry<count;entry++){
  let draw=bins[first+entry];
  if(xy.x<f32(word(draw,0u)) || xy.x>f32(word(draw,1u))+1.0 || xy.y<f32(word(draw,2u)) || xy.y>f32(word(draw,3u))+1.0){continue;}
  // Immutable sources have no sampling side effects. Reject failed destination
  // alpha/depth tests before texture filtering, fog and color interpolation.
  let test=word(draw,5u);let method=(test>>17u)&3u;
  if(method==0u){continue;}
  if((test&0x4000u)!=0u && word(draw,23u)==0u && ((pixel.x>>31u)&1u)!=((test>>15u)&1u)){continue;}
  let stored=select(pixel.y,pixel.y&0xffffffu,word(draw,24u)==49u);
  let flags=word(draw,4u);var weights=vec3f(0.0,0.0,1.0);var at=attributes(draw,2u);var shade=color(draw,2u);var z=0u;
  if((flags&64u)!=0u){
   if((flags&256u)!=0u){continue;}
   let a=vec2f(vf(draw,0u,0u),vf(draw,0u,1u));let b=vec2f(vf(draw,1u,0u),vf(draw,1u,1u));
   let lo=min(vec2i(a),vec2i(b));let span=max(vec2i(1),abs(vec2i(b)-vec2i(a)));let hi=lo+span-vec2i(1);
   let p=vec2i(xy);
   if(any(p<lo)||any(p>hi)){continue;}
   z=word(draw,42u);
   if((method==2u && z<stored) || (method==3u && z<=stored)){continue;}
   let ab=attributes(draw,1u);
   if((flags&2u)!=0u){at=vec4f(bitcast<f32>(texels[word(draw,30u)+(u32(p.x)-word(draw,0u))*scale+sub.x]),bitcast<f32>(texels[word(draw,31u)+(u32(p.y)-word(draw,2u))*scale+sub.y]),1.0,ab.w);}
   else{at=ab;}
   shade=color(draw,1u);
  }else{
   let a=vec2f(vf(draw,0u,0u),vf(draw,0u,1u));let b=vec2f(vf(draw,1u,0u),vf(draw,1u,1u));let c=vec2f(vf(draw,2u,0u),vf(draw,2u,1u));
   if(word(draw,62u)==0u){continue;}
   let winding=bitcast<f32>(word(draw,62u));let inv=bitcast<f32>(word(draw,63u));
   let dx=fp(xy.x-bitcast<f32>(word(draw,56u)));let dy=fp(xy.y-bitcast<f32>(word(draw,57u)));
   let e0=fp(fp(fp(bitcast<f32>(word(draw,58u))*dx)+fp(bitcast<f32>(word(draw,59u))*dy))*winding);
   let e1=fp(fp(fp(bitcast<f32>(word(draw,60u))*dx)+fp(bitcast<f32>(word(draw,61u))*dy))*winding);
   let w0=fp(e0*inv);let w1=fp(e1*inv);let w2=fp(fp(1.0-w0)-w1);
   if(w0 < -1e-4 || w1 < -1e-4 || w2 < -1e-4){continue;}weights=vec3f(w0,w1,w2);
   if(method>=2u || word(draw,22u)==0u){z=depth(draw,weights);}
   if((method==2u && z<stored) || (method==3u && z<=stored)){continue;}
   if((flags&1u)!=0u){shade=weighted(color(draw,0u),color(draw,1u),color(draw,2u),weights);}
   at=weighted(attributes(draw,0u),attributes(draw,1u),attributes(draw,2u),weights);
   if((flags&32u)!=0u){at=vec4f(vec2f(vec2u(vec2i(at.xy))&vec2u(65535u)),1.0,at.w);}
  }
  var src=vec4u(clamp(shade,vec4f(0.0),vec4f(255.0)));
  if((flags&2u)!=0u){let tex=sample(draw,at,flags);let tcc=word(draw,26u)!=0u;let tfx=word(draw,25u);let modulated=min((tex*src)>>vec4u(7u),vec4u(255u));
   switch tfx {case 0u:{src=vec4u(modulated.xyz,select(src.w,modulated.w,tcc));}case 1u:{src=vec4u(tex.xyz,select(src.w,tex.w,tcc));}default:{let rgb=min(modulated.xyz+vec3u(src.w),vec3u(255u));let alpha=select(tex.w,min(tex.w+src.w,255u),tfx==2u);src=vec4u(rgb,select(src.w,alpha,tcc));}}
  }
  if((flags&4u)!=0u){let fog=u32(clamp(at.w,0.0,255.0));src=vec4u(((src.xyz*fog)>>vec3u(8u))+((unpack(word(draw,9u)).xyz*(255u-fog))>>vec3u(8u)),src.w);}
  var rgb=true;var alpha=true;var zw=true;
  if(!alphaPass(test,src.w)){switch((test>>12u)&3u){case 0u:{continue;}case 1u:{zw=false;}case 2u:{rgb=false;alpha=false;}default:{alpha=word(draw,23u)!=0u;zw=false;}}}
  if(rgb||alpha){
   var dst=unpack(pixel.x);if(word(draw,23u)==1u){dst.w=128u;}
   if((flags&8u)!=0u && !((flags&16u)!=0u && (src.w&128u)==0u)){
    let equation=word(draw,6u);let csel=(equation>>4u)&3u;let factor=select(select(word(draw,7u),dst.w,csel==1u),src.w,csel==0u);
    let s=vec3i(src.xyz);let d=vec3i(dst.xyz);let blended=(((choose(equation&3u,s,d)-choose((equation>>2u)&3u,s,d))*i32(factor))>>vec3u(7u))+choose((equation>>6u)&3u,s,d);
    src=vec4u(vec3u(clamp(blended,vec3i(0),vec3i(255))),src.w);
   }
   if(alpha && word(draw,10u)!=0u && word(draw,23u)!=1u){src.w|=128u;}
   var out=pack(src);out=(out&~word(draw,8u))|(pixel.x&word(draw,8u));
   if(!alpha || word(draw,23u)==1u){out=(out&0xffffffu)|(pixel.x&0xff000000u);}pixel.x=out;
  }
  if(zw && word(draw,22u)==0u){if(word(draw,24u)==49u){pixel.y=(pixel.y&0xff000000u)|(z&0xffffffu);}else{pixel.y=z;}}
 }
 pixels[index]=pixel;
}

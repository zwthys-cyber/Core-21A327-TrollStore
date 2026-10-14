// Offline interpretation of protocol 7. No network, device calls or depth guesses.
export function decode64(text) {
  const raw=atob(text);return Uint8Array.from(raw,c=>c.charCodeAt(0));
}
function view(bytes) { return new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength); }
function finite(values) { return values.every(x=>Number.isFinite(x)&&Math.abs(x)<=1e8); }
export function transform(t) {
  const [x,y,z,w]=t.rotation_xyzw,s=t.scale,p=t.translation;
  if (!finite([x,y,z,w,...s,...p]) || Math.abs(x*x+y*y+z*z+w*w-1)>.005) throw Error('Invalid transform');
  const q=[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w),2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w),2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)];
  return [q[0]*s[0],q[1]*s[1],q[2]*s[2],p[0],q[3]*s[0],q[4]*s[1],q[5]*s[2],p[1],q[6]*s[0],q[7]*s[1],q[8]*s[2],p[2]];
}
export function multiply(a,b) {
  const out=Array(12).fill(0);
  for(let r=0;r<3;r++) { for(let c=0;c<3;c++) for(let k=0;k<3;k++) out[r*4+c]+=a[r*4+k]*b[k*4+c];out[r*4+3]=a[r*4+3];for(let k=0;k<3;k++) out[r*4+3]+=a[r*4+k]*b[k*4+3]; }
  return out;
}
export function point(m,p) { return [0,1,2].map(r=>m[r*4+3]+m[r*4]*p[0]+m[r*4+1]*p[1]+m[r*4+2]*p[2]); }
export function inverse(m) {
  const [a,b,c,x,d,e,f,y,g,h,i,z]=m,det=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);
  if(!Number.isFinite(det)||Math.abs(det)<1e-12) throw Error('Singular reference transform');
  const out=[e*i-f*h,c*h-b*i,b*f-c*e,0,f*g-d*i,a*i-c*g,c*d-a*f,0,d*h-e*g,b*g-a*h,a*e-b*d,0].map(v=>v/det);
  for(let r=0;r<3;r++) out[r*4+3]=-out[r*4]*x-out[r*4+1]*y-out[r*4+2]*z;
  if(!out.every(Number.isFinite)) throw Error('Invalid inverse');return out;
}
function rawTransforms(bytes,count) {
  if(bytes.length!==count*48) throw Error('Transform length mismatch');const v=view(bytes),out=[];
  for(let i=0;i<count;i++) { const p=i*48,values=Array.from({length:11},(_,k)=>v.getFloat32(p+k*4,true));out.push(transform({rotation_xyzw:values.slice(0,4),translation:values.slice(4,7),scale:values.slice(8,11)})); }return out;
}
export function referenceSkeleton(resource) {
  const count=resource.declared_bone_count;
  if(!resource.hierarchy_shape_valid||!resource.reference_pose_transform_shape_valid||count<1||count>256) throw Error('Unverified reference skeleton');
  const bytes=decode64(resource.bone_info_base64),v=view(bytes),local=rawTransforms(decode64(resource.reference_pose_base64),count),bones=[],global=[],bind=[];
  if(bytes.length!==count*16) throw Error('Bone record length mismatch');
  for(let i=0;i<count;i++) {
    const parent=v.getInt32(i*16+8,true);if(i===0?parent!==-1:parent<0||parent>=i) throw Error('Invalid bone hierarchy');
    bones.push([v.getUint32(i*16,true),v.getUint32(i*16+4,true)]);global.push(parent<0?local[i]:multiply(global[parent],local[i]));bind.push(inverse(global[i]));
  }return {bones,bind};
}
export function decodeMesh(sample,resource) {
  const d=sample.descriptor,n=d.vertex_count,vertices=decode64(sample.vertex_base64),weights=decode64(sample.weight_base64);
  if(n<1||n>16384||d.vertex_stride!==24||d.weight_stride!==8||vertices.length!==n*24||weights.length!==n*8) throw Error('Unsupported vertex/weight layout');
  const width=d.index_width_low_byte,arrays=sample.index_array_candidates.filter(a=>a.source_prefix_offset===72&&a.stride_hypothesis===width&&a.index_range_shape_valid);
  if(arrays.length!==1||![2,4].includes(width)) throw Error('Missing exact index array');
  const indexBytes=decode64(arrays[0].sample_base64),iv=view(indexBytes),indices=[];
  if(indexBytes.length!==arrays[0].count*width||arrays[0].count%3) throw Error('Invalid triangle array');
  for(let i=0;i<arrays[0].count;i++) { const index=width===2?iv.getUint16(i*2,true):iv.getUint32(i*4,true);if(index>=n) throw Error('Index out of range');indices.push(index); }
  const prefix=decode64(d.section_prefix_base64),sv=view(prefix),sections=[];
  if(d.section_count<1||d.section_count>64||prefix.length<d.section_count*224) throw Error('Incomplete section prefix');
  let nextV=0,nextI=0;const active=new Set();
  for(let j=0;j<d.section_count;j++) {
    const o=j*224,start=sv.getUint32(o+4,true),tri=sv.getUint32(o+8,true),first=sv.getUint32(o+24,true),count=sv.getUint32(o+112,true),boneN=sv.getUint32(o+104,true);
    if(first!==nextV||start!==nextI||!count||!tri||first+count>n||start+tri*3>indices.length) throw Error('Section ranges do not partition the mesh');
    const candidates=sample.section_bone_map_candidates.filter(a=>a.source_prefix_offset===o+96&&a.count===boneN&&a.index_range_shape_valid);
    if(candidates.length!==1) throw Error('Missing section bone palette');
    const paletteBytes=decode64(candidates[0].sample_base64),pv=view(paletteBytes),palette=[];
    if(paletteBytes.length!==boneN*2||!boneN||boneN>256) throw Error('Invalid palette');
    for(let i=0;i<boneN;i++) {const b=pv.getUint16(i*2,true);if(b>=resource.declared_bone_count) throw Error('Palette bone out of range');palette.push(b);}
    for(let i=start;i<start+tri*3;i++) if(indices[i]<first||indices[i]>=first+count) throw Error('Triangle crosses section vertex range');
    for(let i=first;i<first+count;i++) {
      let sum=0;for(let k=0;k<4;k++) {const w=weights[i*8+4+k];sum+=w;if(w) {const b=weights[i*8+k];if(b>=palette.length) throw Error('Weighted palette index out of range');active.add(palette[b]);}}
      if(sum!==255) throw Error('Invalid weight sum');
    }
    sections.push({start,tri,first,count,palette});nextV+=count;nextI+=tri*3;
  }
  if(nextV!==n||nextI!==indices.length) throw Error('Mesh range coverage incomplete');
  const vv=view(vertices),positions=Array.from({length:n},(_,i)=>[0,1,2].map(k=>vv.getFloat32(i*24+8+k*4,true)));
  if(positions.some(p=>!finite(p))) throw Error('Invalid vertex positions');
  return {positions,weights,indices,sections,active};
}
function sameWorld(a,b) { return a.every((v,i)=>Math.abs(v-b[i])<.05); }
export function framePoses(frame) {
  const evidence=frame.mesh_runtime_evidence,resources=new Map(evidence.resource_reference_skeletons.map(r=>[r.asset_candidate_index,r]));
  const components=new Map(evidence.component_pose_bindings.map(c=>[c.asset_candidate_index,c])),cache=new Map(),visiting=new Set();
  function resolve(id) {
    if(cache.has(id)) return cache.get(id);if(visiting.has(id)) throw Error('Attachment master cycle');visiting.add(id);try {
    const resource=resources.get(id);if(!resource) throw Error('Missing reference skeleton');const ref=referenceSkeleton(resource);let world,pose;
    if(id===0) {
      if(frame.bones.length!==ref.bones.length||frame.bones.some((b,i)=>b.index!==i||!b.transform_valid)) throw Error('Primary pose mismatch');
      world=transform(frame.component_transform);pose=frame.bones.map(b=>transform(b.component_space_transform));
    } else {
      const c=components.get(id);if(!c||!c.component_transform_valid) throw Error('Missing component transform');world=transform(c.component_transform);
      if(c.master_index_matches_captured_component) {
        const master=resolve(c.resolved_master_asset_candidate_index),map=c.master_bone_map_candidates.find(m=>m.source==='component-array-candidate');
        if(!map||map.sample_bytes!==ref.bones.length*4||!map.indices_in_resolved_master_reference_range||!sameWorld(world,master.world)) throw Error('Unverified master pose relationship');
        const mv=view(decode64(map.sample_base64));pose=ref.bones.map((b,i)=> {
          const k=mv.getInt32(i*4,true);if(k===-1) return null;
          if(k<0||k>=master.ref.bones.length||b[0]!==master.ref.bones[k][0]||b[1]!==master.ref.bones[k][1]) throw Error('Master bone identity mismatch');
          return master.pose[k];
        });
      } else {
        const own=c.pose_array_candidates.find(p=>p.source_component_offset===0x848&&p.transform_shape_valid&&p.declared_count===ref.bones.length);
        if(!own) throw Error('Current attachment pose unavailable');pose=rawTransforms(decode64(own.sample_base64),ref.bones.length);
      }
    }
    const result={ref,world,pose,skin:pose.map((p,i)=>p?multiply(multiply(world,p),ref.bind[i]):null)};cache.set(id,result);return result;
    } finally { visiting.delete(id); }
  }
  return {resolve,resources};
}
export function deform(mesh,skin) {
  const positions=Array(mesh.positions.length).fill(null);let unknown=0;
  for(const s of mesh.sections) for(let i=s.first;i<s.first+s.count;i++) {
    const p=mesh.positions[i],out=[0,0,0];let valid=true;
    for(let k=0;k<4;k++) {const weight=mesh.weights[i*8+4+k];if(!weight) continue;const bone=s.palette[mesh.weights[i*8+k]],m=skin[bone];if(!m){valid=false;break;}const q=point(m,p);for(let a=0;a<3;a++) out[a]+=q[a]*weight/255;}
    if(valid&&finite(out)) positions[i]=out;else unknown++;
  }
  const triangles=[];for(let i=0;i<mesh.indices.length;i+=3) {const t=mesh.indices.slice(i,i+3);if(t.every(v=>positions[v])) triangles.push(t);}
  return {positions,triangles,unknown};
}
export function buildFrame(report,frameIndex,lod=0,posed=false,includeProps=false) {
  if(report.capture_version!==7||!report.snapshots?.[frameIndex]) throw Error('Requires a protocol 7 capture');
  const frame=report.snapshots[frameIndex],poses=framePoses(frame),names=new Map(frame.name_lookup.names.filter(n=>n.valid_ascii_text).map(n=>[n.name_index,n.text]));
  const catalog=new Map(report.mesh_buffer_snapshots.filter(s=>s.buffer_catalog_id!==null).map(s=>[s.buffer_catalog_id,s])),groups=new Map();
  for(const link of frame.gpu_stream_probe.buffer_references) {
    const d=frame.gpu_stream_probe.descriptors[link.descriptor_index],sample=catalog.get(link.buffer_catalog_id);if(!sample) continue;
    if(sample.descriptor.asset_address!==d.asset_address||sample.descriptor.lod_address!==d.lod_address) throw Error('Buffer identity mismatch');
    if(!groups.has(d.asset_candidate_index)) groups.set(d.asset_candidate_index,[]);groups.get(d.asset_candidate_index).push({d,sample});
  }
  const meshes=[],errors=[];
  for(const [id,entries] of groups) {
    entries.sort((a,b)=>a.d.lod_index-b.d.lod_index);const {d,sample}=entries.find(e=>e.d.lod_index===lod)??entries.at(-1),name=names.get(d.asset_name_index)??String(d.asset_name_index);
    const prop=/parachute|wingsuit|smoke|glide/i.test(name);if(prop&&!includeProps) continue;
    try {
      const resource=poses.resources.get(id);if(!resource) throw Error('No reference skeleton');const mesh=decodeMesh(sample,resource);
      const geometry=posed?deform(mesh,poses.resolve(id).skin):{positions:mesh.positions,triangles:Array.from({length:mesh.indices.length/3},(_,i)=>mesh.indices.slice(i*3,i*3+3)),unknown:0};
      if(posed) {const origin=frame.component_transform.translation;geometry.positions=geometry.positions.map(p=>p?p.map((v,i)=>v-origin[i]):null);}
      meshes.push({...geometry,name,prop,lod:d.lod_index,vertices:mesh.positions.length,totalTriangles:mesh.indices.length/3,id});
    } catch(error) { errors.push({name,message:error.message}); }
  }
  return {meshes,errors,frame};
}

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {execFileSync} from 'node:child_process';
import {Script} from 'node:vm';
import {transform,inverse,point,multiply,referenceSkeleton,decodeMesh,framePoses,buildFrame} from '../scripts/model_mesh_preview.mjs';
const t=(x=0,y=0,z=0)=>({rotation_xyzw:[0,0,0,1],translation:[x,y,z],scale:[1,1,1]});
function close(a,b){a.forEach((x,i)=>assert.ok(Math.abs(x-b[i])<1e-6,`${a} != ${b}`));}
const b64=b=>Buffer.from(b).toString('base64');
function fixture(){
  const info=Buffer.alloc(32);info.writeUInt32LE(100,0);info.writeInt32LE(-1,8);info.writeUInt32LE(101,16);info.writeUInt32LE(2,20);
  const ref=Buffer.alloc(96);for(let i=0;i<2;i++){ref.writeFloatLE(1,i*48+12);for(let k=0;k<3;k++)ref.writeFloatLE(1,i*48+32+k*4);}ref.writeFloatLE(10,16);ref.writeFloatLE(5,68);
  const resource={asset_candidate_index:0,declared_bone_count:2,hierarchy_shape_valid:true,reference_pose_transform_shape_valid:true,bone_info_base64:b64(info),reference_pose_base64:b64(ref)};
  const verts=Buffer.alloc(72),weights=Buffer.alloc(24);for(let i=0;i<3;i++){verts.writeFloatLE(10+i,i*24+8);verts.writeFloatLE(5,i*24+12);weights[i*8]=1;weights[i*8+4]=255;}
  const indices=Buffer.alloc(6);indices.writeUInt16LE(1,2);indices.writeUInt16LE(2,4);const palette=Buffer.alloc(4);palette.writeUInt16LE(1,2);
  const section=Buffer.alloc(256);section.writeUInt32LE(1,8);section.writeUInt32LE(2,104);section.writeUInt32LE(2,108);section.writeUInt32LE(3,112);
  const descriptor={asset_candidate_index:99,asset_address:400,lod_address:500,asset_name_index:200,lod_index:0,vertex_count:3,vertex_stride:24,weight_stride:8,index_width_low_byte:2,section_count:1,section_prefix_base64:b64(section)};
  const sample={buffer_catalog_id:0,descriptor,vertex_base64:b64(verts),weight_base64:b64(weights),index_array_candidates:[{source_prefix_offset:72,stride_hypothesis:2,index_range_shape_valid:true,count:3,sample_base64:b64(indices)}],section_bone_map_candidates:[{source_prefix_offset:96,count:2,index_range_shape_valid:true,sample_base64:b64(palette)}]};
  const map=Buffer.alloc(8);map.writeInt32LE(0,0);map.writeInt32LE(1,4);
  const component={asset_candidate_index:1,component_transform_valid:true,component_transform:t(100),master_index_matches_captured_component:true,resolved_master_asset_candidate_index:0,master_bone_map_candidates:[{source:'component-array-candidate',sample_bytes:8,indices_in_resolved_master_reference_range:true,sample_base64:b64(map)}],pose_array_candidates:[]};
  const frame={component_transform:t(100),bones:[{index:0,transform_valid:true,component_space_transform:t(20)},{index:1,transform_valid:true,component_space_transform:t(20,5)}],mesh_runtime_evidence:{resource_reference_skeletons:[resource,{...resource,asset_candidate_index:1}],component_pose_bindings:[component]},name_lookup:{names:[{name_index:200,text:'Synthetic shirt',valid_ascii_text:true}]},gpu_stream_probe:{descriptors:[{...descriptor,asset_candidate_index:0}],buffer_references:[{descriptor_index:0,buffer_catalog_id:0}]}};
  return {report:{capture_version:7,snapshots:[frame],mesh_buffer_snapshots:[sample]},frame,sample,resource,component,map};
}
test('rotation, nonuniform scale, affine inverse and singular rejection',()=>{
  const m=transform({rotation_xyzw:[0,0,Math.SQRT1_2,Math.SQRT1_2],translation:[10,20,30],scale:[2,3,4]});
  close(point(m,[1,2,3]),[4,22,42]);close(point(inverse(m),[4,22,42]),[1,2,3]);
  close(point(multiply(m,inverse(m)),[1,2,3]),[1,2,3]);assert.throws(()=>inverse(transform({...t(),scale:[0,1,1]})),/Singular/);
});
test('hierarchical bind and correct snapshot-local asset lookup',()=>{
  const {report,resource}=fixture();const ref=referenceSkeleton(resource);close(point(ref.bind[1],[10,5,0]),[0,0,0]);
  const rest=buildFrame(report,0),posed=buildFrame(report,0,0,true);assert.equal(rest.meshes.length,1);assert.equal(posed.errors.length,0);
  close(rest.meshes[0].positions[0],[10,5,0]);close(posed.meshes[0].positions[0],[20,5,0]);assert.equal(posed.meshes[0].triangles.length,1);
});
test('weighted unknown bones omit vertices and triangles without renormalization',()=>{
  const {report,frame,component,map}=fixture();frame.gpu_stream_probe.descriptors[0].asset_candidate_index=1;map.writeInt32LE(-1,4);component.master_bone_map_candidates[0].sample_base64=b64(map);
  const model=buildFrame(report,0,0,true);assert.equal(model.meshes[0].unknown,3);assert.equal(model.meshes[0].triangles.length,0);
});
test('full FName number and master cycles fail closed',()=>{
  const {frame,component}=fixture(),r=frame.mesh_runtime_evidence.resource_reference_skeletons[1];const info=Buffer.from(r.bone_info_base64,'base64');info.writeUInt32LE(3,20);r.bone_info_base64=b64(info);
  assert.throws(()=>framePoses(frame).resolve(1),/identity/);component.resolved_master_asset_candidate_index=1;assert.throws(()=>framePoses(frame).resolve(1),/cycle/);
});
test('malformed weights, section partitions, index and palette ranges reject',()=>{
  for(const mutate of [
    s=>{const b=Buffer.from(s.weight_base64,'base64');b[4]=254;s.weight_base64=b64(b);},
    s=>{const b=Buffer.from(s.weight_base64,'base64');b[0]=2;s.weight_base64=b64(b);},
    s=>{const b=Buffer.from(s.descriptor.section_prefix_base64,'base64');b.writeUInt32LE(1,24);s.descriptor.section_prefix_base64=b64(b);},
    s=>{const a=s.index_array_candidates[0],b=Buffer.from(a.sample_base64,'base64');b.writeUInt16LE(3,0);a.sample_base64=b64(b);}
  ]){const {sample,resource}=fixture();mutate(sample);assert.throws(()=>decodeMesh(sample,resource));}
});
test('standalone packaging escapes capture text and parses its inline script',()=>{
  const tmp=fs.mkdtempSync(path.join(os.tmpdir(),'core-model-preview-'));
  try{const {report}=fixture();report.date='</script><script>alert(1)</script>';const input=path.join(tmp,'report.json'),output=path.join(tmp,'preview.html');fs.writeFileSync(input,JSON.stringify(report));
    execFileSync('python3',['scripts/package_model_preview.py',input,output],{cwd:path.resolve(import.meta.dirname,'..')});const html=fs.readFileSync(output,'utf8');
    assert.ok(!html.includes('__CAPTURE_JSON__'));assert.ok(!html.includes('__MODEL_PREVIEW_MODULE__'));assert.ok(!html.includes('alert(1)</script>'));
    const scripts=[...html.matchAll(/<script(?: [^>]*)?>([\s\S]*?)<\/script>/g)];assert.equal(scripts.length,2);new Script(scripts[1][1]);
    assert.equal(JSON.parse(scripts[0][1]).date,report.date);
  }finally{fs.rmSync(tmp,{recursive:true,force:true});}
});

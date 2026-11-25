/*
 * Copyright 2026 Feifan He for CodeWeavers
 * Copyright 2026 Paweł Kołodziejski
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <metal_common>
#include <metal_graphics>
#include <metal_texture>
#include <metal_math>
#include "dxmt_command_constants.hpp"

using namespace metal;
using namespace dxmt;

[[kernel]] void clear_texture_1d_uint(
    texture1d<uint, access::read_write> tex [[texture(0)]],
    constant uint4& value [[buffer(1)]],
    ushort pos [[thread_position_in_grid]]
) {
  uint width = tex.get_width();
  if(width > pos) {
    tex.write(value, pos);
  }
}

[[kernel]] void clear_texture_1d_array_uint(
    texture1d_array<uint, access::read_write> tex [[texture(0)]],
    constant uint4& value [[buffer(1)]],
    ushort2 pos [[thread_position_in_grid]]
) {
  uint width = tex.get_width();
  uint array_len = tex.get_array_size();
  if(width > pos.x && array_len > pos.y) {
    tex.write(value, pos.x, pos.y);
  }
}

[[kernel]] void clear_texture_2d_uint(
    texture2d<uint, access::read_write> tex [[texture(0)]],
    constant uint4& value [[buffer(1)]],
    ushort2 pos [[thread_position_in_grid]]
) {
  uint width = tex.get_width();
  uint height = tex.get_height();
  if(width > pos.x && height > pos.y) {
    tex.write(value, pos);
  }
}

[[kernel]] void clear_texture_2d_array_uint(
    texture2d_array<uint, access::read_write> tex [[texture(0)]],
    constant uint4& value [[buffer(1)]],
    ushort3 pos [[thread_position_in_grid]]
) {
  uint width = tex.get_width();
  uint height = tex.get_height();
  uint array_len = tex.get_array_size();
  if(width > pos.x && height > pos.y && array_len > pos.z) {
    tex.write(value, pos.xy, pos.z);
  }
}

[[kernel]] void clear_texture_3d_uint(
    texture3d<uint, access::read_write> tex [[texture(0)]],
    constant uint4& value [[buffer(1)]],
    ushort3 pos [[thread_position_in_grid]]
) {
  uint width = tex.get_width();
  uint height = tex.get_height();
  uint depth = tex.get_depth();
  if(width > pos.x && height > pos.y && depth > pos.z) {
    tex.write(value, pos);
  }
}

struct DXMTClearTextureBufferUInt {
  uint4 value;
  uint offset;
  uint size;
};

[[kernel]] void clear_texture_buffer_uint(
    texture_buffer<uint, access::read_write> tex [[texture(0)]],
    constant DXMTClearTextureBufferUInt& args [[buffer(1)]],
    uint pos [[thread_position_in_grid]]
) {
  uint width = tex.get_width();
  uint offset = pos + args.offset;
  if (args.size > pos && width > offset) {
    tex.write(args.value, offset);
  }
}

[[kernel]] void clear_buffer_uint(
    device uint* tex [[buffer(0)]],
    constant uint& size [[buffer(2)]],
    constant uint4& value [[buffer(1)]],
    uint pos [[thread_position_in_grid]]
) {
  if(pos < size) {
    tex[pos] = value.x;
  }
}

[[kernel]] void clear_texture_1d_float(
    texture1d<float, access::read_write> tex [[texture(0)]],
    constant float4& value [[buffer(1)]],
    ushort pos [[thread_position_in_grid]]
) {
  uint width = tex.get_width();
  if(width > pos) {
    tex.write(value, pos);
  }
}

[[kernel]] void clear_texture_1d_array_float(
    texture1d_array<float, access::read_write> tex [[texture(0)]],
    constant float4& value [[buffer(1)]],
    ushort2 pos [[thread_position_in_grid]]
) {
  uint width = tex.get_width();
  uint array_len = tex.get_array_size();
  if(width > pos.x && array_len > pos.y) {
    tex.write(value, pos.x, pos.y);
  }
}

[[kernel]] void clear_texture_2d_float(
    texture2d<float, access::read_write> tex [[texture(0)]],
    constant float4& value [[buffer(1)]],
    ushort2 pos [[thread_position_in_grid]]
) {
  uint width = tex.get_width();
  uint height = tex.get_height();
  if(width > pos.x && height > pos.y) {
    tex.write(value, pos);
  }
}

[[kernel]] void clear_texture_2d_array_float(
    texture2d_array<float, access::read_write> tex [[texture(0)]],
    constant float4& value [[buffer(1)]],
    ushort3 pos [[thread_position_in_grid]]
) {
  uint width = tex.get_width();
  uint height = tex.get_height();
  uint array_len = tex.get_array_size();
  if(width > pos.x && height > pos.y && array_len > pos.z) {
    tex.write(value, pos.xy, pos.z);
  }
}

[[kernel]] void clear_texture_3d_float(
    texture3d<float, access::read_write> tex [[texture(0)]],
    constant float4& value [[buffer(1)]],
    ushort3 pos [[thread_position_in_grid]]
) {
  uint width = tex.get_width();
  uint height = tex.get_height();
  uint depth = tex.get_depth();
  if(width > pos.x && height > pos.y && depth > pos.z) {
    tex.write(value, pos);
  }
}

struct DXMTClearTextureBufferFloat {
  float4 value;
  uint offset;
  uint size;
};

[[kernel]] void clear_texture_buffer_float(
    texture_buffer<float, access::read_write> tex [[texture(0)]],
    constant DXMTClearTextureBufferFloat& args [[buffer(1)]],
    uint pos [[thread_position_in_grid]]
) {
  uint width = tex.get_width();
  uint offset = pos + args.offset;
  if (args.size > pos && width > offset) {
    tex.write(args.value, offset);
  }
}

struct clear_data {
  float4 position [[position]];
  uint array_index [[render_target_array_index]];
};

[[vertex]] clear_data vs_clear_rt(
  ushort id [[vertex_id]],
  ushort instance_id [[instance_id]]
) {
  clear_data output;
  float2 uv = float2((id << 1) & 2, id & 2);
  output.position = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
  output.array_index = instance_id;
  return output;
}

[[fragment]] float4 fs_clear_rt_float(
  constant float4& clear_value [[buffer(kCustomBufferArgumentIndex0)]]
) {
  return clear_value;
}

[[fragment]] uint4 fs_clear_rt_uint(
  constant float4& clear_value [[buffer(kCustomBufferArgumentIndex0)]]
) {
  return (uint4)clear_value;
}

[[fragment]] int4 fs_clear_rt_sint(
  constant float4& clear_value [[buffer(kCustomBufferArgumentIndex0)]]
) {
  return (int4)clear_value;
}

struct depth_out {
  float depth [[depth(any)]];
};

[[fragment]] depth_out fs_clear_rt_depth (
  constant float4& clear_value [[buffer(kCustomBufferArgumentIndex0)]]
) {
  return {clear_value.x};
}

/* Same computation as the depth bias airconv adds to pixel shaders, for draws
   without one; SM50_SHADER_DEPTH_BIAS picks the variant */
float biased_depth(float z, float4 depth_bias) {
  float max_slope = max(abs(dfdx(z)), abs(dfdy(z)));
  float bias = depth_bias.x + depth_bias.y * max_slope;
  float bias_clamp = depth_bias.z;
  if (bias_clamp > 0)
    bias = min(bias_clamp, bias);
  else if (bias_clamp < 0)
    bias = max(bias_clamp, bias);
  return saturate(z + bias);
}

struct depth_out_greater {
  float depth [[depth(greater)]];
};

struct depth_out_less {
  float depth [[depth(less)]];
};

[[fragment]] depth_out fs_depth_bias_any (
  float4 position [[position]],
  constant float4& depth_bias [[buffer(kDepthBiasArgumentIndex)]]
) {
  return {biased_depth(position.z, depth_bias)};
}

[[fragment]] depth_out_greater fs_depth_bias_greater (
  float4 position [[position]],
  constant float4& depth_bias [[buffer(kDepthBiasArgumentIndex)]]
) {
  return {max(position.z, biased_depth(position.z, depth_bias))};
}

[[fragment]] depth_out_less fs_depth_bias_less (
  float4 position [[position]],
  constant float4& depth_bias [[buffer(kDepthBiasArgumentIndex)]]
) {
  return {min(position.z, biased_depth(position.z, depth_bias))};
}

struct present_data {
  float4 position [[position]];
  float2 uv [[user(coord)]];
};

[[vertex]] present_data vs_present_quad(ushort id [[vertex_id]]) {
  present_data output;
  float2 uv = float2((id << 1) & 2, id & 2);
	output.position = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
  output.uv = uv;
  return output;
}

constant constexpr float PQ_M1 = 0.1593017578125;
constant constexpr float PQ_M2 = 78.84375;
constant constexpr float PQ_C1 = 0.8359375;
constant constexpr float PQ_C2 = 18.8515625;
constant constexpr float PQ_C3 = 18.6875;

float linear_to_pq(float linear) {
  linear = linear / 10000;
  return pow((PQ_C1 + PQ_C2 * pow(abs(linear), PQ_M1)) / (1.0f + PQ_C3 * pow(abs(linear), PQ_M1)), PQ_M2);
}

float3 linear_to_pq(float3 linear) {
  linear = linear / 10000;
  return pow((PQ_C1 + PQ_C2 * pow(abs(linear), PQ_M1)) / (1.0f + PQ_C3 * pow(abs(linear), PQ_M1)), PQ_M2);
}

float pq_to_linear(float norm) {
  return 10000 * pow(abs(max(pow(norm, 1.0 / PQ_M2) -PQ_C1, 0.0f) / (PQ_C2 - PQ_C3 * pow(abs(norm), 1.0f / PQ_M2))), 1.0f / PQ_M1 );
}

float3 pq_to_linear(float3 norm) {
  return 10000 * pow(abs(max(pow(norm, 1.0 / PQ_M2) -PQ_C1, 0.0f) / (PQ_C2 - PQ_C3 * pow(abs(norm), 1.0f / PQ_M2))), 1.0f / PQ_M1 );
}

// See BT.2408 Annex 5
float EETF(float E, float Lw, float Lb, float Lmax, float Lmin) {
  float pq_Lb = linear_to_pq(Lb);
  float pqdiff_LwLb = linear_to_pq(Lw) - pq_Lb;
  float min_lum = (linear_to_pq(Lmin) - pq_Lb) / pqdiff_LwLb;
  float max_lum = (linear_to_pq(Lmax) - pq_Lb) / pqdiff_LwLb;
  E = (E - pq_Lb) / pqdiff_LwLb;
  float KS = 1.5 * max_lum - 0.5;
  float b = min_lum;
  if (E >= KS) {
    float T = (E - KS) /  (1 - KS);
    float T_2 = T * T;
    float T_3 = T_2 * T;
    E = (2 * T_3 - 3 * T_2 + 1) * KS 
        + (T_3 - 2 * T_2 + T) * (1 - KS)
        + (- 2 * T_3 + 3 * T_2) * max_lum;
  }
  E = E + b * pow(1 - E, 4);
  return E * pqdiff_LwLb + pq_Lb;
}

constant bool present_backbuffer_size_matched [[function_constant(kPresentFCIndex_BackbufferSizeMatched)]];
constant bool present_backbuffer_is_srgb [[function_constant(kPresentFCIndex_BackbufferIsSRGB)]];
constant bool present_hdr_pq [[function_constant(kPresentFCIndex_HDRPQ)]];
constant bool present_with_hdr_metadata [[function_constant(kPresentFCIndex_WithHDRMetadata)]];
constant bool present_backbuffer_is_ms [[function_constant(kPresentFCIndex_BackbufferIsMS)]];
constant bool present_backbuffer_is_not_ms = !present_backbuffer_is_ms;
constant bool present_gamma_enabled [[function_constant(kPresentFCIndex_GammaEnabled)]];

constexpr sampler s(coord::normalized);
constexpr sampler gamma_sampler(coord::normalized, filter::linear, address::clamp_to_edge);

struct DXMTPresentMetadata {
  float edr_scale;
  float max_content_luminance;
  float max_display_luminance;
};

float3 to_srgb(float3 linear) {
  return select(1.055 * pow(linear, 1.0 / 2.4) - 0.055, linear * 12.92, linear < 0.0031308); 
}

[[fragment]] float4 fs_present_quad(
    present_data input [[stage_in]],
    texture2d<float, access::sample> source [[texture(0), function_constant(present_backbuffer_is_not_ms)]],
    texture2d_ms<float, access::read> source_ms [[texture(0), function_constant(present_backbuffer_is_ms)]],
    texture2d<float, access::sample> gamma_lut [[texture(1), function_constant(present_gamma_enabled)]],
    constant DXMTPresentMetadata& meta [[buffer(0)]]
) {
  float4 output = float4(0);
  if (present_backbuffer_is_ms) {
    const uint count = source_ms.get_num_samples();
    for (uint i = 0; i < count; i++) {
      output += source_ms.read(uint2(input.position.xy), i);
    }
    output /= count;
  } else {
    output = present_backbuffer_size_matched
      ? source.read(uint2(input.position.xy))
      : source.sample(s, input.uv);
  }
  float3 output_rgb = output.xyz;
  if (present_gamma_enabled && !present_hdr_pq && !present_with_hdr_metadata) {
    output_rgb = float3(
        gamma_lut.sample(gamma_sampler, float2(output_rgb.r, 0.5)).r,
        gamma_lut.sample(gamma_sampler, float2(output_rgb.g, 0.5)).g,
        gamma_lut.sample(gamma_sampler, float2(output_rgb.b, 0.5)).b);
  }
  float edr_scale = meta.edr_scale;
  if (present_backbuffer_is_srgb)
    output_rgb = to_srgb(output_rgb);
  if (present_hdr_pq)
    output_rgb = pq_to_linear(output_rgb);
  else if (present_with_hdr_metadata)
    output_rgb = output_rgb * 80; // to nits
  if (present_with_hdr_metadata) {
    // tone mapping: BT.2408 EETF + maxRGB
    float max_content_luminance = meta.max_content_luminance;
    float min_content_luminance = 1; // in case of division by 0
    float max_display_luminance = meta.max_display_luminance;
    float min_display_luminance = 0;
    float3 clamped = clamp(output_rgb, float3(min_content_luminance), float3(max_content_luminance));
    float3 clamped_pq = linear_to_pq(clamped);
    float maxRGB = max3(clamped_pq.x, clamped_pq.y, clamped_pq.z);
    float maxRGB2 = EETF(
      maxRGB, max_content_luminance, min_content_luminance, max_display_luminance, min_display_luminance
    );
    output_rgb = pq_to_linear(maxRGB2) / pq_to_linear(maxRGB) * clamped;
  }
  output_rgb *= edr_scale;
  if (present_hdr_pq)
    output_rgb = linear_to_pq(output_rgb);
  else if (present_with_hdr_metadata)
    output_rgb = output_rgb / 80;
  return float4(output_rgb, output.w);
}

struct DXMTDispatchArguments {
  uint x;
  uint y;
  uint z;
};

struct DXMTTSDispatchMarshal {
  constant uint2& draw_arguments; // (vertex|index_count, instance_count)
  device DXMTDispatchArguments& dispatch_arguments_out;
  ulong max_object_threadgroups;
  ushort control_point_count;
  ushort patch_per_group;
  uint end_of_command;
};

[[vertex]] void ts_draw_arguments_marshal(
    constant DXMTTSDispatchMarshal* tasks [[buffer(kCustomBufferArgumentIndex0)]]
) {
  uint index = 0;
  for(;;) {
    constant DXMTTSDispatchMarshal& task = tasks[index];

    device DXMTDispatchArguments& output = task.dispatch_arguments_out;

    uint patch_count_per_instance = task.draw_arguments.x / (uint)task.control_point_count;
    uint x = (patch_count_per_instance - 1) / task.patch_per_group + 1;
    if ((task.draw_arguments.x * task.draw_arguments.y) == 0 ||
        x * task.draw_arguments.y > task.max_object_threadgroups) {
      output.x = 0;
      output.y = 0;
      output.z = 0;
    } else {
      output.x = x;
      output.y = task.draw_arguments.y;
      output.z = 1;
    }

    if (task.end_of_command)
      break;
    index++;
  };
}

struct DXMTGSDispatchMarshal {
  constant uint2& draw_arguments; // (vertex|index_count, instance_count)
  device DXMTDispatchArguments& dispatch_arguments_out;
  ulong max_object_threadgroups;
  uint vertex_count_per_warp;
  uint end_of_command;
};

[[vertex]] void gs_draw_arguments_marshal(
    constant DXMTGSDispatchMarshal* tasks [[buffer(kCustomBufferArgumentIndex0)]]
) {
  uint index = 0;
  for(;;) {
    constant DXMTGSDispatchMarshal& task = tasks[index];

    device DXMTDispatchArguments& output = task.dispatch_arguments_out;

    ulong x = (task.draw_arguments.x - 1) / task.vertex_count_per_warp + 1;
    if (task.draw_arguments.x == 0 || task.draw_arguments.y == 0 ||
        x * task.draw_arguments.y > task.max_object_threadgroups) {
      output.x = 0;
      output.y = 0;
      output.z = 0;
    } else {
      output.x = (uint)x;
      output.y = task.draw_arguments.y;
      output.z = 1;
    }

    if (task.end_of_command)
      break;
    index++;
  };
}

// The structs of the stream output functions have the layout of SO_DRAW_TASK,
// SO_DRAW_RECORD and SO_DRAW_AUTO_TASK in dxmt_context.hpp

struct DXMTStreamOutputSlot {
  device uint* filled;
  device uint* filled_out;
  ulong buffer;
  uint size;
  uint stride;
};

struct DXMTStreamOutputRecord {
  ulong buffer_handle[4];
  ulong index_buffer;
  uint vertex_limit;
  uint vertex_count;
  int base_vertex;
  uint index_limit;
  uint index_size;
  uint reserved;
  device const uint* strip_cuts;
};

// The structs and functions of the strip cut scan have the layout of
// STRIP_CUTS_TASK in dxmt_context.hpp. The cut index of a strip ends the
// strip, and the next one starts with a new primitive of even winding, so
// where a primitive of an indexed strip starts and its place among the
// primitives of the draw depend on every cut before it. The scan takes the
// positions of the draw in blocks of kStripCutsBlock. For each block it
// records the last cut and the primitives without a cut index, and then
// carries both over the blocks, so that a function that needs them for a
// primitive reads its block's carry and scans no further back than the
// start of its block.
struct DXMTStripCutsTask {
  device const uint* arguments;
  device const uchar* index_buffer;
  device uint* cuts;
  uint index_limit;
  uint index_size;
  uint count;
  uint start;
  uint window;
  uint blocks;
};

constant uint kStripCutsBlock = 1024;

static void
strip_cuts_draw(constant DXMTStripCutsTask& task, thread uint& count, thread uint& start) {
  count = task.arguments ? task.arguments[0] : task.count;
  start = task.arguments ? task.arguments[2] : task.start;
}

// An index past the end of the index buffer is 0, not a cut
static bool
strip_cut_at(constant DXMTStripCutsTask& task, uint start, uint position) {
  ulong index = (ulong)start + position;
  if (index >= task.index_limit)
    return false;
  if (task.index_size == 2)
    return ((device const ushort*)task.index_buffer)[index] == 0xffff;
  return ((device const uint*)task.index_buffer)[index] == 0xffffffff;
}

// Of the primitives that start in [begin, end): those without a cut index,
// and the last cut in [begin, end) plus 1, or 0
static uint
strip_cuts_scan(
    constant DXMTStripCutsTask& task, uint count, uint start, uint begin, uint end, thread uint& last
) {
  uint valid = 0, run = 0;
  ulong scan_end = (ulong)end + task.window - 1;
  if (scan_end > count)
    scan_end = count;
  last = 0;
  for (uint position = begin; position < scan_end; position++) {
    bool cut = strip_cut_at(task, start, position);
    run = cut ? 0 : run + 1;
    if (cut && position < end)
      last = position + 1;
    if (run >= task.window)
      valid++;
  }
  return valid;
}

// One point for each block: its last cut plus 1 and its primitives without a
// cut into cuts[4 + 4 * vertex_id] and cuts[5 + 4 * vertex_id]
[[vertex]] void strip_cuts_blocks(
    constant DXMTStripCutsTask& task [[buffer(kCustomBufferArgumentIndex0)]],
    uint vertex_id [[vertex_id]]
) {
  if (vertex_id >= task.blocks)
    return;
  uint count, start;
  strip_cuts_draw(task, count, start);
  ulong begin = (ulong)vertex_id * kStripCutsBlock;
  ulong end = begin + kStripCutsBlock < count ? begin + kStripCutsBlock : count;
  uint last = 0, valid = 0;
  if (begin < count)
    valid = strip_cuts_scan(task, count, start, (uint)begin, (uint)end, last);
  task.cuts[4 + 4 * vertex_id] = last;
  task.cuts[5 + 4 * vertex_id] = valid;
}

// Carries the blocks over: the last cut before each block plus 1 into
// cuts[6 + 4 * block], the primitives without a cut before it into
// cuts[7 + 4 * block], the blocks it carried into cuts[0] and the primitives
// without a cut of the draw into cuts[1]. Returns the latter. A draw with
// more blocks than the scratch buffer has room for and no cut in those it
// carried counts every primitive after them and sets cuts[2]: the cuts
// after them are not seen. It reads nothing it writes, so it may run more
// than once.
static ulong
strip_cuts_carry(constant DXMTStripCutsTask& task, uint count) {
  uint needed = (uint)(((ulong)count + kStripCutsBlock - 1) / kStripCutsBlock);
  uint blocks = needed > task.blocks ? task.blocks : needed;
  uint last = 0;
  ulong valid = 0;
  for (uint block = 0; block < blocks; block++) {
    task.cuts[6 + 4 * block] = last;
    task.cuts[7 + 4 * block] = (uint)valid;
    uint block_last = task.cuts[4 + 4 * block];
    if (block_last)
      last = block_last;
    valid += task.cuts[5 + 4 * block];
  }
  bool plain = blocks < needed && !last && valid == (ulong)blocks * kStripCutsBlock;
  if (plain)
    valid = (ulong)count - task.window + 1;
  task.cuts[0] = blocks;
  task.cuts[1] = valid > 0xffffffff ? 0xffffffff : (uint)valid;
  task.cuts[2] = plain;
  return valid;
}

// The primitives without a cut of a draw: with blocks from strip_cuts_blocks,
// or of a draw of one block, which it scans here
static ulong
strip_cuts_valid(constant DXMTStripCutsTask& task, uint count, uint start) {
  if (task.blocks)
    return strip_cuts_carry(task, count);
  uint last;
  return strip_cuts_scan(task, count, start, 0, count < kStripCutsBlock ? count : kStripCutsBlock, last);
}

// Between strip_cuts_blocks and a draw through the geometry pipeline, whose
// object function reads the carries
[[vertex]] void strip_cuts_prologue(
    constant DXMTStripCutsTask& task [[buffer(kCustomBufferArgumentIndex0)]]
) {
  uint count, start;
  strip_cuts_draw(task, count, start);
  strip_cuts_carry(task, count);
}

struct DXMTStreamOutputCounters {
  ulong written;
  ulong needed;
};

struct DXMTStreamOutputTask {
  DXMTStreamOutputSlot slots[4];
  device uint* arguments;
  device DXMTStreamOutputRecord* record_out;
  device uint* draw_arguments_out;
  device DXMTStreamOutputCounters* counters_out;
  ulong index_buffer;
  uint index_buffer_size;
  uint index_size;
  uint count;
  uint instance_count;
  uint start;
  int base_vertex;
  uint start_instance;
  uint topology_lead;
  uint topology_step;
  uint vertices_per_primitive;
  uint unbound;
  uint rasterized;
  DXMTStripCutsTask cuts;
};

// Counts what a stream output draw writes when the encoding thread cannot:
// the arguments of the draw, a filled size or the cuts of an indexed strip
// are only known here. Writes the filled sizes after the draw, what the
// draw's vertex function reads, the arguments of the draw and the counters
// of stream 0. An indexed strip draws a primitive at each position, and the
// vertex function leaves out those with a cut index; the others are written
// one after another. It reads nothing it writes, so it may run more than
// once.
[[vertex]] void so_draw_prologue(
    constant DXMTStreamOutputTask& task [[buffer(kCustomBufferArgumentIndex0)]]
) {
  uint count = task.count;
  uint instance_count = task.instance_count;
  uint start = task.start;
  int base_vertex = task.base_vertex;
  uint start_instance = task.start_instance;
  if (task.arguments) {
    count = task.arguments[0];
    instance_count = task.arguments[1];
    start = task.arguments[2];
    if (task.index_size) {
      base_vertex = (int)task.arguments[3];
      start_instance = task.arguments[4];
    } else {
      start_instance = task.arguments[3];
    }
  }

  ulong positions = count > task.topology_lead ? (count - task.topology_lead) / task.topology_step : 0;
  ulong vertex_count = positions * task.vertices_per_primitive;
  ulong primitives = positions;
  if (task.cuts.window && positions)
    primitives = strip_cuts_valid(task.cuts, count, start);
  if (vertex_count > 0xfffffffful) {
    primitives = 0;
    vertex_count = 0;
  }
  ulong needed = primitives * instance_count;
  ulong written = task.unbound ? 0 : needed;
  for (uint slot = 0; slot < 4; slot++) {
    if (!task.slots[slot].filled)
      continue;
    uint filled = *task.slots[slot].filled;
    ulong available = task.slots[slot].size > filled ? task.slots[slot].size - filled : 0;
    ulong room = available / ((ulong)task.slots[slot].stride * task.vertices_per_primitive);
    if (room < written)
      written = room;
  }

  device DXMTStreamOutputRecord& record = *task.record_out;
  for (uint slot = 0; slot < 4; slot++) {
    if (!task.slots[slot].filled) {
      record.buffer_handle[slot] = 0;
      continue;
    }
    uint filled = *task.slots[slot].filled;
    record.buffer_handle[slot] = task.slots[slot].buffer + filled;
    *task.slots[slot].filled_out = filled + (uint)(written * task.vertices_per_primitive * task.slots[slot].stride);
  }
  ulong vertex_limit = written * task.vertices_per_primitive;
  record.vertex_limit = vertex_limit > 0xffffffff ? 0xffffffff : (uint)vertex_limit;
  record.vertex_count = (uint)(primitives * task.vertices_per_primitive);
  record.base_vertex = task.index_size ? base_vertex : (int)start;
  record.index_size = task.index_size;
  record.reserved = 0;
  record.strip_cuts = task.cuts.window ? task.cuts.cuts : nullptr;
  ulong first_index = (ulong)start * task.index_size;
  if (task.index_size && first_index < task.index_buffer_size) {
    record.index_buffer = task.index_buffer + first_index;
    record.index_limit = (uint)((task.index_buffer_size - first_index) / task.index_size);
  } else {
    record.index_buffer = 0;
    record.index_limit = 0;
  }

  // MTLDrawPrimitivesIndirectArguments; without rasterization nothing to draw
  // when nothing is written
  task.draw_arguments_out[0] = written || task.rasterized ? (uint)vertex_count : 0;
  task.draw_arguments_out[1] = instance_count;
  task.draw_arguments_out[2] = 0;
  task.draw_arguments_out[3] = start_instance;

  if (task.counters_out) {
    task.counters_out[0].written = written;
    task.counters_out[0].needed = needed;
    for (uint stream = 1; stream < 4; stream++) {
      task.counters_out[stream].written = 0;
      task.counters_out[stream].needed = 0;
    }
  }
}

struct DXMTStreamOutputAutoTask {
  device uint* filled;
  device uint* arguments_out;
  uint offset;
  uint stride;
  device DXMTDispatchArguments* dispatch_out;
  ulong max_object_threadgroups;
  uint vertex_count_per_warp;
  uint reserved;
};

// Writes the arguments of a DrawAuto from a filled size the encoding thread
// does not know, and through the geometry pipeline the dispatch arguments as
// gs_draw_arguments_marshal does, which runs before the draws of the encoder
[[vertex]] void so_draw_auto(
    constant DXMTStreamOutputAutoTask& task [[buffer(kCustomBufferArgumentIndex0)]]
) {
  uint filled = *task.filled;
  uint count = filled > task.offset ? (filled - task.offset) / task.stride : 0;
  task.arguments_out[0] = count;
  task.arguments_out[1] = 1;
  task.arguments_out[2] = 0;
  task.arguments_out[3] = 0;
  if (!task.dispatch_out)
    return;
  ulong x = count ? (count - 1) / task.vertex_count_per_warp + 1 : 0;
  bool draw = x && x <= task.max_object_threadgroups;
  task.dispatch_out->x = draw ? (uint)x : 0;
  task.dispatch_out->y = draw ? 1 : 0;
  task.dispatch_out->z = draw ? 1 : 0;
}

struct DXMTGeometryStreamOutputRecord {
  ulong buffer_handle[4];
  device uint* counts;
  device uint* firsts;
  uint limit[4];
};

struct DXMTGeometryStreamOutputTask {
  DXMTStreamOutputSlot slots[4];
  device const uint* counts;
  device uint* firsts;
  device DXMTGeometryStreamOutputRecord* record_out;
  device DXMTStreamOutputCounters* counters_out;
  device const uint* dispatch;
  device const uint* arguments;
  device uint* omitted;
  device uint* blocks;
  uint threadgroups;
  uint mesh_threadgroups;
  uint vertices_per_primitive;
  uint streams;
  uint slot_streams[4];
  uint unbound;
  uint block_count;
};

// The counts of a draw are added up in blocks of so many mesh threadgroups,
// one thread each: one GPU thread that walks the counts of 64000 takes some
// 25 ms, and Metal reports a GPU hang for it
constant uint kGeometryStreamOutputBlock = 1024;

static ulong
so_geometry_count(constant DXMTGeometryStreamOutputTask& task) {
  ulong threadgroups = task.dispatch ? (ulong)task.dispatch[0] * task.dispatch[1] : task.threadgroups;
  return threadgroups * task.mesh_threadgroups;
}

// Of a draw whose counts take more than one block: the sums of block
// vertex_id, one for each stream, into blocks[8 * vertex_id + stream]
[[vertex]] void so_geometry_block_sums(
    constant DXMTGeometryStreamOutputTask& task [[buffer(kCustomBufferArgumentIndex0)]],
    uint vertex_id [[vertex_id]]
) {
  ulong count = so_geometry_count(task);
  ulong begin = (ulong)vertex_id * kGeometryStreamOutputBlock;
  ulong end = begin + kGeometryStreamOutputBlock < count ? begin + kGeometryStreamOutputBlock : count;
  uint sums[4] = {0, 0, 0, 0};
  for (ulong i = begin; i < end; i++) {
    for (uint stream = 0; stream < task.streams; stream++)
      sums[stream] += task.counts[i * task.streams + stream];
  }
  for (uint stream = 0; stream < 4; stream++)
    task.blocks[8 * vertex_id + stream] = sums[stream];
}

// Of a draw whose counts take more than one block: the positions of block
// vertex_id from the position of its first in each stream,
// blocks[8 * vertex_id + 4 + stream]
[[vertex]] void so_geometry_block_firsts(
    constant DXMTGeometryStreamOutputTask& task [[buffer(kCustomBufferArgumentIndex0)]],
    uint vertex_id [[vertex_id]]
) {
  ulong count = so_geometry_count(task);
  ulong begin = (ulong)vertex_id * kGeometryStreamOutputBlock;
  ulong end = begin + kGeometryStreamOutputBlock < count ? begin + kGeometryStreamOutputBlock : count;
  uint firsts[4];
  for (uint stream = 0; stream < 4; stream++)
    firsts[stream] = task.blocks[8 * vertex_id + 4 + stream];
  for (ulong i = begin; i < end; i++) {
    for (uint stream = 0; stream < task.streams; stream++) {
      task.firsts[i * task.streams + stream] = firsts[stream];
      firsts[stream] += task.counts[i * task.streams + stream];
    }
  }
}

// Between the two passes of a geometry shader that streams out: adds up what
// the mesh threadgroups of the draw counted of each stream, in the order of
// the draw, into the position of each one's first primitive, stops each
// stream at the room of its slots, and writes what the write pass reads, the
// filled sizes after the draw and the counters of the streams. With more than
// one block it adds up the sums of so_geometry_block_sums into the first
// positions of the blocks, from which so_geometry_block_firsts writes the
// others. It reads nothing it writes, so it may run more than once. An
// indirect draw with more object threadgroups than the counts have room for
// has none, which it tells the encoding thread.
[[vertex]] void so_geometry_prologue(
    constant DXMTGeometryStreamOutputTask& task [[buffer(kCustomBufferArgumentIndex0)]]
) {
  if (task.dispatch && !so_geometry_count(task) && task.arguments[0] && task.arguments[1])
    *task.omitted = 1;
  ulong needed[4] = {0, 0, 0, 0};
  if (task.block_count > 1) {
    for (uint block = 0; block < task.block_count; block++) {
      for (uint stream = 0; stream < task.streams; stream++) {
        task.blocks[8 * block + 4 + stream] = (uint)needed[stream];
        needed[stream] += task.blocks[8 * block + stream];
      }
    }
  } else {
    ulong count = so_geometry_count(task);
    for (ulong i = 0; i < count; i++) {
      for (uint stream = 0; stream < task.streams; stream++) {
        task.firsts[i * task.streams + stream] = (uint)needed[stream];
        needed[stream] += task.counts[i * task.streams + stream];
      }
    }
  }
  ulong written[4];
  for (uint stream = 0; stream < 4; stream++)
    written[stream] = (task.unbound & (1u << stream)) ? 0 : needed[stream];
  for (uint slot = 0; slot < 4; slot++) {
    if (!task.slots[slot].filled)
      continue;
    uint stream = task.slot_streams[slot];
    uint filled = *task.slots[slot].filled;
    ulong available = task.slots[slot].size > filled ? task.slots[slot].size - filled : 0;
    ulong room = available / ((ulong)task.slots[slot].stride * task.vertices_per_primitive);
    if (room < written[stream])
      written[stream] = room;
  }

  device DXMTGeometryStreamOutputRecord& record = *task.record_out;
  for (uint slot = 0; slot < 4; slot++) {
    if (!task.slots[slot].filled) {
      record.buffer_handle[slot] = 0;
      continue;
    }
    uint filled = *task.slots[slot].filled;
    record.buffer_handle[slot] = task.slots[slot].buffer + filled;
    *task.slots[slot].filled_out =
        filled + (uint)(written[task.slot_streams[slot]] * task.vertices_per_primitive * task.slots[slot].stride);
  }
  for (uint stream = 0; stream < 4; stream++)
    record.limit[stream] = written[stream] > 0xffffffff ? 0xffffffff : (uint)written[stream];
  record.counts = (device uint*)task.counts;
  record.firsts = task.firsts;

  if (task.counters_out) {
    for (uint stream = 0; stream < 4; stream++) {
      task.counters_out[stream].written = written[stream];
      task.counters_out[stream].needed = needed[stream];
    }
  }
}

struct depth_stencil_out {
  float depth [[depth(any)]];
  uint stencil [[stencil]]; 
};

struct packed_d32s8x24 {
  float depth;
  uchar stencil;
  uchar unused[3];
};

struct linear_texture_desc {
  uint bytes_per_row;
  uint bytes_per_image;
};

[[fragment]] depth_stencil_out fs_copy_from_buffer_d32s8(
  present_data input [[stage_in]],
  device char* buffer [[buffer(kCustomBufferArgumentIndex0)]],
  constant linear_texture_desc& desc [[buffer(kCustomBufferArgumentIndex1)]]
) {
  depth_stencil_out result;
  uint2 pos = uint2(input.position.xy);
  uint buffer_offset = pos.x * sizeof(packed_d32s8x24) 
                        + pos.y * desc.bytes_per_row;
  packed_d32s8x24 data = *reinterpret_cast<device packed_d32s8x24 *>(buffer + buffer_offset);
  result.depth = data.depth;
  result.stencil = data.stencil;
  return result;
}

[[fragment]] depth_stencil_out fs_copy_from_buffer_d24s8(
  present_data input [[stage_in]],
  device char* buffer [[buffer(kCustomBufferArgumentIndex0)]],
  constant linear_texture_desc& desc [[buffer(kCustomBufferArgumentIndex1)]]
) {
  depth_stencil_out result;
  uint2 pos = uint2(input.position.xy);
  uint buffer_offset = pos.x * sizeof(uint) 
                        + pos.y * desc.bytes_per_row;
  uint data = *reinterpret_cast<device uint *>(buffer + buffer_offset);
  result.depth = float(data & 0xffffff) / float(0xffffff);
  result.stencil = data >> 24;
  return result;
}

[[kernel]] void cs_copy_to_buffer_d32s8(
  texture2d<float, access::read> tex_depth [[texture(0)]],
  texture2d<uint, access::read> tex_stencil [[texture(1)]],
  device char* buffer [[buffer(0)]],
  constant linear_texture_desc& desc [[buffer(1)]],
  ushort2 pos [[thread_position_in_grid]]
) {
  uint width = tex_depth.get_width();
  uint height = tex_depth.get_height();
  uint buffer_offset = pos.x * sizeof(packed_d32s8x24) 
                        + pos.y * desc.bytes_per_row;
  if (width > pos.x && height > pos.y) {
    auto dst = reinterpret_cast<device packed_d32s8x24 *>(buffer + buffer_offset);
    dst->depth = tex_depth.read(pos).x;
    dst->stencil = tex_stencil.read(pos).x;
  }
}

[[kernel]] void cs_copy_to_buffer_d24s8(
  texture2d<float, access::read> tex_depth [[texture(0)]],
  texture2d<uint, access::read> tex_stencil [[texture(1)]],
  device char* buffer [[buffer(0)]],
  constant linear_texture_desc& desc [[buffer(1)]],
  ushort2 pos [[thread_position_in_grid]]
) {
  uint width = tex_depth.get_width();
  uint height = tex_depth.get_height();
  uint buffer_offset = pos.x * sizeof(uint) 
                        + pos.y * desc.bytes_per_row;
  if (width > pos.x && height > pos.y) {
    auto dst = reinterpret_cast<device uint *>(buffer + buffer_offset);
    float depth = tex_depth.read(pos).x;
    uint stencil = tex_stencil.read(pos).x;
    *dst = (uint(0xffffff * clamp(depth, 0.0, 1.0)) & 0xffffff) | (stencil << 24);
  }
}

struct DXMTClearFloatMetadata {
  float4 value;
  uint2 offset;
  uint2 size;
};

struct DXMTClearUintMetadata {
  uint4 value;
  uint2 offset;
  uint2 size;
};

[[kernel]] void cs_clear_texture2d_float(
    texture2d<float, access::write> tex [[texture(0)]],
    constant DXMTClearFloatMetadata& meta [[buffer(1)]],
    uint2 pos [[thread_position_in_grid]]
) {
  tex.write(meta.value, meta.offset + pos);
}

[[kernel]] void cs_clear_texture2d_array_float(
    texture2d_array<float, access::write> tex [[texture(0)]],
    constant DXMTClearFloatMetadata& meta [[buffer(1)]],
    uint3 pos [[thread_position_in_grid]]
) {
  tex.write(meta.value, meta.offset + pos.xy, pos.z);
}

[[kernel]] void cs_clear_texture3d_float(
    texture3d<float, access::write> tex [[texture(0)]],
    constant DXMTClearFloatMetadata& meta [[buffer(1)]],
    uint2 pos [[thread_position_in_grid]]
) {
  auto depth = tex.get_depth();
  for (uint i = 0; i < depth; i++) {
    uint2 final_pos = meta.offset + pos;
    tex.write(meta.value, uint3(final_pos.x, final_pos.y, i));
  }
}

[[kernel]] void cs_clear_texture2d_uint(
    texture2d<uint, access::write> tex [[texture(0)]],
    constant DXMTClearUintMetadata& meta [[buffer(1)]],
    uint2 pos [[thread_position_in_grid]]
) {
  tex.write(meta.value, meta.offset + pos);
}

[[kernel]] void cs_clear_texture2d_array_uint(
    texture2d_array<uint, access::write> tex [[texture(0)]],
    constant DXMTClearUintMetadata& meta [[buffer(1)]],
    uint3 pos [[thread_position_in_grid]]
) {
  tex.write(meta.value, meta.offset + pos.xy, pos.z);
}

[[kernel]] void cs_clear_texture3d_uint(
    texture3d<uint, access::write> tex [[texture(0)]],
    constant DXMTClearUintMetadata& meta [[buffer(1)]],
    uint2 pos [[thread_position_in_grid]]
) {
  auto depth = tex.get_depth();
  for (uint i = 0; i < depth; i++) {
    uint2 final_pos = meta.offset + pos;
    tex.write(meta.value, uint3(final_pos.x, final_pos.y, i));
  }
}

[[kernel]] void cs_clear_tbuffer_float(
    texture_buffer<float, access::write> tex [[texture(0)]],
    constant DXMTClearFloatMetadata& meta [[buffer(1)]],
    uint pos [[thread_position_in_grid]]
) {
  tex.write(meta.value, meta.offset.x + pos);
}

[[kernel]] void cs_clear_tbuffer_uint(
    texture_buffer<uint, access::write> tex [[texture(0)]],
    constant DXMTClearUintMetadata& meta [[buffer(1)]],
    uint pos [[thread_position_in_grid]]
) {
  tex.write(meta.value, meta.offset.x + pos);
}

[[kernel]] void cs_clear_buffer_float(
    device float* buffer [[buffer(0)]],
    constant DXMTClearFloatMetadata& meta [[buffer(1)]],
    uint pos [[thread_position_in_grid]]
) {
  buffer[meta.offset.x + pos] = meta.value.x;
}

[[kernel]] void cs_clear_buffer_uint(
    device uint* buffer [[buffer(0)]],
    constant DXMTClearUintMetadata& meta [[buffer(1)]],
    uint pos [[thread_position_in_grid]]
) {
  buffer[meta.offset.x + pos] = meta.value.x;
}

[[kernel]] void cs_downscale_dilated_mv(
  uint2 pos [[thread_position_in_grid]],
  constant float2& mv_scale [[buffer(0)]],
  texture2d<float, access::read> dilated [[texture(0)]],
  texture2d<float, access::write> downscaled [[texture(1)]]
) {
  float2 source_size = float2(dilated.get_width(), dilated.get_height());
  float2 viewport_size = float2(downscaled.get_width(), downscaled.get_height());
  float2 scale = viewport_size / source_size;
  float2 sample_uv = (float2(pos) + 0.5) / viewport_size;
  uint2 sample_pos = uint2(sample_uv * source_size);
  float2 hi_mv = dilated.read(sample_pos).xy;
  float2 hi_mv_pixel = hi_mv * mv_scale;
  float2 lo_mv_pixel = hi_mv_pixel * scale;
  downscaled.write(lo_mv_pixel.xyxy, pos); 
}

[[kernel]] void tile_barrier(ushort2 pos [[thread_position_in_threadgroup]]) {
  // empty
}

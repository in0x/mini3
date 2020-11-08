struct VertexIn
{
	float3 pos_local : POSITION;
	float3 normal	 : NORMAL;
	float3 tangent	 : TANGENT;
	float2 uv		 : TEXCOORD;
};

struct VertexOut
{
	float4 pos_sp : SV_POSITION;
	float4 color : COLOR;
};

#ifdef VERTEX_SHADER

cbuffer cbPerObject : register(b0)
{
	float4x4 g_model;
	float4x4 g_view_proj;
};

VertexOut vs_main(VertexIn vsIn)
{
	VertexOut vsOut;

	float4x4 mvp = mul(g_model, g_view_proj);
	float4 pos_sp = mul(float4(vsIn.pos_local, 1.0f), mvp);

	vsOut.pos_sp = pos_sp;
	vsOut.color = float4(1.0f, 0.0f, 0.0f, 1.0f);

	return vsOut;
}

#endif // VERTEX_SHADER

#ifdef PIXEL_SHADER

float4 ps_main(VertexOut psIn) : SV_TARGET
{
	return psIn.color;
}

#endif // PIXEL_SHADER
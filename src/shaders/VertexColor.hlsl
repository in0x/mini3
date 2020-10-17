struct VertexIn
{
	float3 pos_local : POSITION;
	float4 color : COLOR;
};

struct VertexOut
{
	float4 pos_sp : SV_POSITION;
	float4 color : COLOR;
};

#ifdef VERTEX_SHADER

cbuffer cbPerObject : register(b0)
{
	float4x4 g_ModelViewProj;
};

VertexOut vs_main(VertexIn vsIn)
{
	VertexOut vsOut;

	vsOut.pos_sp = mul(float4(vsIn.pos_local, 1.0f), g_ModelViewProj);

	vsOut.color = vsIn.color;

	return vsOut;
}

#endif // VERTEX_SHADER

#ifdef PIXEL_SHADER

float4 ps_main(VertexOut psIn) : SV_TARGET
{
	return psIn.color;
}

#endif // PIXEL_SHADER
#define MaxLights 16

struct Material
{
    float4 DiffuseAlbedo;
    float3 FresnelR0;
    float Shininess;    // Shininess = 1 - roughness
};

struct Light
{
    float3 Strength;
    float FalloffStart; // point/spot light only
    float3 Direction;   // directional/spot light only
    float FalloffEnd;   // point/spot light only
    float3 Position;    // point light only
    float SpotPower;    // spot light only
};

float CalcAttenuation(float d, float falloffStart, float falloffEnd)
{
    // Linear attenuation
    return saturate((falloffEnd - d) / (falloffEnd - falloffStart));
}

// Schlick gives an approximation to Fresnel reflectance (see pg. 233 "Real-Time Rendering 3rd Ed.").
// R0 = ( (n-1)/(n+1) )^2, where n is the index of refraction.
float3 SchlickFresnel(float3 R0, float3 normal, float3 lightVec)
{
    float cosIncidentAngle = saturate(dot(normal, lightVec));
    float f0 = 1.f - cosIncidentAngle;
    float3 reflectPercent = R0 + (1.f - R0) * (f0 * f0 * f0 * f0 * f0);

    return reflectPercent;
}

float3 BlinnPhong(float3 lightStrength, float3 lightVec, 
            float3 normal, float3 toEye, Material mat)
{
    // The lower the m is, the rougher the surface is. So m represents the shininess.
    const float m = mat.Shininess * 256.f;
    float3 halfVec = normalize(toEye + lightVec);
    
    float roughnessFactor = (m + 8.f) / 8.f * pow(max(dot(normal, halfVec), 0.f), m);
    float3 fresnelFactor = SchlickFresnel(mat.FresnelR0, halfVec, lightVec);
    
    float3 specAlbedo = fresnelFactor * roughnessFactor;
    
    // Our spec formula goes outside [0,1] range, but we are 
    // doing LDR rendering.  So scale it down a bit.
    specAlbedo = specAlbedo / (specAlbedo + 1.0f);
    
    return (mat.DiffuseAlbedo.rgb + specAlbedo) * lightStrength;
}

//---------------------------------------------------------------------------------------
// Evaluates the lighting equation for directional lights.
//---------------------------------------------------------------------------------------
float3 ComputeDirectionalLight(Light L, Material mat, float3 normal, float3 toEye)
{
    // The light vector aims opposite the direction the light rays travel.
    float3 lightVec = -L.Direction;
    
    // Scale light down by Lambert's cosine law.
    float lambertCosFactor = max(dot(normal, lightVec), 0.f);
    float3 lightStrength = lambertCosFactor * L.Strength;
    
    return BlinnPhong(lightStrength, lightVec, normal, toEye, mat);
}

//---------------------------------------------------------------------------------------
// Evaluates the lighting equation for point lights.
//---------------------------------------------------------------------------------------
float3 ComputePointLight(Light L, Material mat, float3 pos, float3 normal, float3 toEye)
{
    // The vector from the surface to the light.
    float3 lightVec = L.Position - pos;    
    float d = length(lightVec);
    
    // Improve performance
    if (d > L.FalloffEnd)
    {
        return 0.f;
    }
    
    // Normalization
    lightVec /= d;
    
    // Scale light down by Lambert's cosine law.
    float lambertCosFactor = max(dot(normal, lightVec), 0.f);
    float3 lightStrength = lambertCosFactor * L.Strength;
    
    float att = CalcAttenuation(d, L.FalloffStart, L.FalloffEnd);
    lightStrength *= att;
    
    return BlinnPhong(lightStrength, lightVec, normal, toEye, mat);
}

//---------------------------------------------------------------------------------------
// Evaluates the lighting equation for spot lights.
//---------------------------------------------------------------------------------------
float3 ComputeSpotLight(Light L, Material mat, float3 pos, float3 normal, float3 toEye)
{
    // The vector from the surface to the light.
    float3 lightVec = L.Position - pos;
    
    // The distance from surface to light.
    float d = length(lightVec);
    
    // Range test.
    if (d > L.FalloffEnd)
    {
        return 0.0f;
    }
    
    // Normalization
    lightVec /= d;
    
    // Scale light down by Lambert's cosine law.
    float lambertCosFactor = max(dot(normal, lightVec), 0.f);
    float3 lightStrength = lambertCosFactor * L.Strength;

    // Attenuate light by distance.
    float att = CalcAttenuation(d, L.FalloffStart, L.FalloffEnd);
    lightStrength *= att;
    
    // Scale by spotlight
    float spotFactor = pow(max(dot(-lightVec, L.Direction), 0.f), L.SpotPower);
    lightStrength *= spotFactor;
    
    return BlinnPhong(lightStrength, lightVec, normal, toEye, mat);
}

float4 ComputeLighting(Light gLights[MaxLights], Material mat,
                       float3 pos, float3 normal, float3 toEye,
                       float3 shadowFactor)
{
    float3 result = 0.0f;

    int i = 0;

#if (NUM_DIR_LIGHTS > 0)
    for(i = 0; i < NUM_DIR_LIGHTS; ++i)
    {
        result += shadowFactor[i] * ComputeDirectionalLight(gLights[i], mat, normal, toEye);
    }
#endif

#if (NUM_POINT_LIGHTS > 0)
    for(i = NUM_DIR_LIGHTS; i < NUM_DIR_LIGHTS+NUM_POINT_LIGHTS; ++i)
    {
        result += ComputePointLight(gLights[i], mat, pos, normal, toEye);
    }
#endif

#if (NUM_SPOT_LIGHTS > 0)
    for(i = NUM_DIR_LIGHTS + NUM_POINT_LIGHTS; i < NUM_DIR_LIGHTS + NUM_POINT_LIGHTS + NUM_SPOT_LIGHTS; ++i)
    {
        result += ComputeSpotLight(gLights[i], mat, pos, normal, toEye);
    }
#endif 

    return float4(result, 0.0f);
}
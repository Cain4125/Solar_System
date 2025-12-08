#version 330 core

out vec4 FragColor;

uniform vec2  uResolution;
uniform float uMoonOffset;   
uniform float uOcclusion;   

void main()
{  
    vec2 p = (gl_FragCoord.xy - 0.5 * uResolution)
           / min(uResolution.x, uResolution.y);
            
    float sunR  = 0.35;
    float moonR = 0.33;

    vec2 sunCenter  = vec2(0.0, 0.0);
    vec2 moonCenter = vec2(uMoonOffset * 0.8, 0.0); 

    float rs = length(p - sunCenter);   
    float rm = length(p - moonCenter);  
     
    if (rm < moonR) {
        FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
     
    float skyDark = mix(1.0, 0.22, clamp(uOcclusion, 0.0, 1.0));
     
    float rim = smoothstep(sunR, sunR - 0.015, rs);
     
    float coronaBase = exp(-4.0 * max(0.0, rs - sunR));
     
    float ang = atan(p.y, p.x);

    float ray1 = pow(max(0.0, cos(ang * 10.0 + 0.7)), 6.0);
    float ray2 = pow(max(0.0, cos(ang * 17.0 - 1.3)), 4.0);
    float ray3 = pow(max(0.0, cos(ang * 27.0 + 3.1)), 3.0);

    float rays = 0.5 * ray1 + 0.3 * ray2 + 0.2 * ray3;
     
    float jitter = 0.5 + 0.5 * sin(ang * 37.0 + rs * 8.0);
    rays *= jitter;
     
    float coronaStrength = mix(0.25, 1.0, clamp(uOcclusion, 0.0, 1.0));
    float corona = coronaBase * (0.3 + 0.7 * rays) * coronaStrength;

    float intensity = rim + corona;
    vec3 col = vec3(intensity);
     
    if (rs < sunR - 0.002) { 
        col = vec3(1.0, 1.0, 0.9);
    }
     
    col *= skyDark;

    FragColor = vec4(col, 1.0);
}

#include <stdio.h>
#include <malloc.h>
#include <string.h>

struct output {
    int x;
    int y;
    int w;
    int h;
    FILE* f;
} *outputs;
int output_count=0;

struct plane {
    int horizontal_subsampling;
    int vertical_subsampling;
    /* plane size in bytes is w*h*num/denom  */
} planes[4];
int plane_count;

char y4m_header[1024];
int w, h;

char frame_header[1024];

const char* chroma_subsampling;

unsigned char* buffer;
                

/* Output to out->f cropped video frame plane contained in buffer. */
void output_part_of_video_frame(struct output *out, int j) {
    int vertical_subsampling   = planes[j].vertical_subsampling;
    int horizontal_subsampling = planes[j].horizontal_subsampling;
    

    int nvw = w / horizontal_subsampling;
    int nvh = h / vertical_subsampling;
    int nx = out->x / horizontal_subsampling;
    int ny = out->y / vertical_subsampling;
    int nw = out->w / horizontal_subsampling;
    int nh = out->h / vertical_subsampling;

    int u;

    for(u=ny; u<ny+nh; ++u) {
        fwrite(buffer + u*nvw + nx, nw, 1, out->f);
    }

}

int main(int argc, char* argv[]) {
    int i;
    if (argc<=1) {
        fprintf(stderr, "Usage: yuv4mpegsplit 0_0_1024_768.yuv 1024_0_1024_768.yuv x_y_width_height.yuv... < input.yuv\n");
        return 1;
    }
    outputs = (struct output*) malloc(argc*sizeof(*outputs));
    if (!outputs) {
        fprintf(stderr, "Unable to allocate memory for outputs\n");
        return 2;
    }

    output_count=argc-1;
    for(i=1; i<argc; ++i) {
        struct output *out = &outputs[i-1];
        if(sscanf(argv[i], "%i_%i_%i_%i", &out->x, &out->y, &out->w, &out->h)!=4) {
            fprintf(stderr, "Malformed output name \"%s\". Should be x_y_width_height.yuv\n", argv[i]);
            return 3;
        }
        out->f = fopen(argv[i], "wb");
        if (!out->f) {
            fprintf(stderr, "Unable to open file \"%s\"\n", argv[i]);
            perror("fopen");
            return 4;
        }
        printf("Opened output %s: x=%d y=%d w=%d h=%d\n", argv[i], out->x, out->y, out->w, out->h);
    }

    fgets(y4m_header, sizeof y4m_header, stdin);
    printf("y4m header: %s", y4m_header);

    if(memcmp(y4m_header, "YUV4MPEG2 ", 10)) {
        fprintf(stderr, "Input signature is not \"YUV4MPEG2\": %s\n", y4m_header);
        return 5;
    }
    memset(y4m_header, ' ', 10); // Space out signature

    char* p, *p2;
    p = strstr(y4m_header, " W");
    if (!p) { fprintf(stderr, "Width not found\n"); return 6; }
    sscanf(p, " W%d", &w);
    for(p=p+1; *p!=' ' && *p!='\0'; ++p) *p=' '; // Space out width
    
    p = strstr(y4m_header, " H");
    if (!p) { fprintf(stderr, "Height not found\n"); return 6; }
    sscanf(p, " H%d", &h);
    for(p=p+1; *p!=' ' && *p!='\0'; ++p) *p=' '; // Space out height

    /* Remove double spaces */
    for(p=y4m_header, p2=y4m_header; p[1]!='\0'; ++p) {
        if(p[0]==' ' && p[1]==' ') {
            // Skip spaces
        } else {
            *p2=p[1];
            ++p2;
        }
    }
    *p2='\0';

    for(i=0; i<output_count; ++i) {
        struct output *out = &outputs[i];
        char out_header[1024];

        sprintf(out_header, "YUV4MPEG2 W%d H%d %s", out->w, out->h, y4m_header);
        printf("Header for output %d: %s", i, out_header);
        fprintf(out->f, "%s", out_header);
    }


    p = strstr(y4m_header, " I");
    if(p) {
        if(!memcmp(p, " I?", 3)) {
            fprintf(stderr, "Considering input as progressive\n");
        } else
        if(!memcmp(p, " Ip", 3)) {
            // OK, only progressive is supported now
        } else {
            fprintf(stderr, "Interlaced input is not supported\n");
            return 7;
        }
    }

    p = strstr(y4m_header, " C");
    if(p) {
        chroma_subsampling = p+2;
    } else {
        chroma_subsampling = "420jpeg";        
    }

    planes[0].horizontal_subsampling=1; planes[0].vertical_subsampling=1;
    if(strstr(chroma_subsampling, "420")) {
        planes[1].horizontal_subsampling=2; planes[1].vertical_subsampling=2;
        planes[2].horizontal_subsampling=2; planes[2].vertical_subsampling=2;
        plane_count=3;
    } else
    if(strstr(chroma_subsampling, "411")) {
        planes[1].horizontal_subsampling=4; planes[1].vertical_subsampling=1;
        planes[2].horizontal_subsampling=4; planes[2].vertical_subsampling=1;
        plane_count=3;
    } else
    if(strstr(chroma_subsampling, "422")) {
        planes[1].horizontal_subsampling=2; planes[1].vertical_subsampling=1;
        planes[2].horizontal_subsampling=2; planes[2].vertical_subsampling=1;
        plane_count=3;
    } else
    if(strstr(chroma_subsampling, "444alpha")) {
        planes[1].horizontal_subsampling=1; planes[1].vertical_subsampling=1;
        planes[2].horizontal_subsampling=1; planes[2].vertical_subsampling=1;
        planes[3].horizontal_subsampling=1; planes[3].vertical_subsampling=1;
        plane_count=4;
    } else
    if(strstr(chroma_subsampling, "444")) {
        planes[1].horizontal_subsampling=1; planes[1].vertical_subsampling=1;
        planes[2].horizontal_subsampling=1; planes[2].vertical_subsampling=1;
        plane_count=3;
    } else
    if(strstr(chroma_subsampling, "mono")) {
        plane_count=1;
    } else {
        fprintf(stderr, "Unknown chroma subsampling\n");
        return 8;
    }

    /* Sanity check for input sizes */
    for (i=0; i<output_count; ++i) {
        struct output *out = &outputs[i];
        if(out->x < 0 || out->x + out->w > w) { fprintf(stderr, "Horiz out of bounds for output %d\n", i); return 11;}
        if(out->y < 0 || out->y + out->h > h) { fprintf(stderr, "Vert out of bounds for output %d\n", i); return 11;}
        if(out->w <= 0) { fprintf(stderr, "Invalid width for output %d\n", i); return 11; }
        if(out->h <= 0) { fprintf(stderr, "Invalid height for output %d\n", i); return 11; }
        int j;
        for(j=0; j<plane_count; ++j) {
            if(out->w % planes[j].horizontal_subsampling) {
                fprintf(stderr, "Output width should be divisible by %d\n", planes[j].horizontal_subsampling);
                return 11;
            }
            if(out->h % planes[j].vertical_subsampling) {
                fprintf(stderr, "Output height should be divisible by %d\n", planes[j].vertical_subsampling);
                return 11;
            }
        }
    }

    buffer = (unsigned char*) malloc(h*w);

    while(!feof(stdin)) {
        if(!fgets(frame_header, sizeof frame_header, stdin)) {
            break;
        }
        if(memcmp(frame_header, "FRAME", 5)) {
            fprintf(stderr, "Frame does not begin with \"FRAME\"\n");
            return 9;
        }            
        for(i=0; i<output_count; ++i) {
            fprintf(outputs[i].f, "%s", frame_header);
        }
        int j;
        for(j=0; j<plane_count; ++j) {
            int size = (h/planes[j].vertical_subsampling) * (w/planes[j].horizontal_subsampling);
            fread(buffer, 1, size, stdin);
            for(i=0; i<output_count; ++i) {
                output_part_of_video_frame(&outputs[i], j);
            }
        }
        for(i=0; i<output_count; ++i) {
            fflush(outputs[i].f);
        }
    }

    free(outputs);
    free(buffer);
    return 0;    
}

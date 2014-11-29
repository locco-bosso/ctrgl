/*
    Boost Software License - Version 1.0 - August 17th, 2003

    Permission is hereby granted, free of charge, to any person or organization
    obtaining a copy of the software and accompanying documentation covered by
    this license (the "Software") to use, reproduce, display, distribute,
    execute, and transmit the Software, and to prepare derivative works of the
    Software, and to permit third-parties to whom the Software is furnished to
    do so, all subject to the following:

    The copyright notices in the Software and this entire statement, including
    the above license grant, this restriction and the following disclaimer,
    must be included in all copies of the Software, in whole or in part, and
    all derivative works of the Software, unless such copies or derivative
    works are solely in the form of machine-executable object code generated by
    a source language processor.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
    SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
    FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
    ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
    DEALINGS IN THE SOFTWARE.
*/

#ifndef CTR_GL_C
#error This is a private CTRGL implementation file. Please use #include <gl.h> instead.
#endif

/* buffer matrix list - used for stereo */
#define BUFFERMATRIXLIST_SIZE 4

typedef struct
{
    u32 offset;
    GLmatricesStateCTR matrices;
    GLint projectionUniform;
}
bufferMatrix_s;

static bufferMatrix_s bufferMatrixList[BUFFERMATRIXLIST_SIZE];
static int bufferMatrixListLength;

static void loadIdentity4x4(float* m)
{
    memset(m, 0x00, 4 * 4 * sizeof(float));

    m[0] = 1.0f;
    m[5] = 1.0f;
    m[10] = 1.0f;
    m[15] = 1.0f;
}

static void multMatrix4x4(float* m1, float* m2, float* m)
{
    int i, j;

    for (i = 0; i < 4; i++)
        for(j = 0; j < 4; j++)
        {
            m[i + j * 4] = (m1[0 + j * 4] * m2[i + 0 * 4])
                    + (m1[1 + j * 4] * m2[i + 1 * 4])
                    + (m1[2 + j * 4] * m2[i + 2 * 4])
                    + (m1[3 + j * 4] * m2[i + 3 * 4]);
        }
}

static void flushMatrices()
{
    /* projection is complicated, because we have to remember it for further stereo processing */
    if ((dirtyMatrices & (1 << GL_PROJECTION)) && shaderState.program->projectionUniform != -1)
    {
        if (bufferMatrixListLength < BUFFERMATRIXLIST_SIZE)
        {
            /* get current offset in command buffer */
            GPUCMD_GetBuffer(NULL, NULL, &bufferMatrixList[bufferMatrixListLength].offset);

            /* remember the settings */
            memcpy(&bufferMatrixList[bufferMatrixListLength].matrices, &matricesState, sizeof(GLmatricesStateCTR));
            bufferMatrixList[bufferMatrixListLength].projectionUniform = shaderState.program->projectionUniform;

            bufferMatrixListLength++;
        }

        glUniformMatrix4fv(shaderState.program->projectionUniform, 1, GL_TRUE, (float*) matricesState.projection);
        dirtyMatrices &= ~(1 << GL_PROJECTION);
    }

    /* modelview, on the other hand, is trivial */
    if ((dirtyMatrices & (1 << GL_MODELVIEW)) && shaderState.program->modelviewUniform != -1)
    {
        glUniformMatrix4fv(shaderState.program->modelviewUniform, 1, GL_TRUE, (float*) matricesState.modelview);
        dirtyMatrices &= ~(1 << GL_MODELVIEW);
    }
}

static void adjustBufferMatrices(GLmat4x4 transformation, float axialShift)
{
    int i;
    u32* buffer;
    u32 offset;

    GPUCMD_GetBuffer(&buffer, NULL, &offset);

    for (i = 0; i < bufferMatrixListLength; i++)
    {
        u32 o = bufferMatrixList[i].offset;

        if (o + 2 < offset) //TODO : better check, need to account for param size
        {
            const GLmatricesStateCTR* st = &bufferMatrixList[i].matrices;

            if (st->stereoMode == GL_STEREO_NONE_CTR)
                continue;

            if (st->stereoMode == GL_STEREO_PERSPECTIVE_CTR)
            {
                const float frustumShift = axialShift * (st->stereoParams.perspective.nearZ / st->screenZ);
                const float modelTranslation = axialShift;

                transformation[0][2] = frustumShift * st->stereoParams.perspective.scale;
                transformation[0][3] = modelTranslation * st->stereoParams.perspective.scale;
            }
            else
            {
                const float frustumShift = -axialShift * st->stereoParams.ortho.skew;
                /*const float modelTranslation = axialShift * (st->screenZ * st->stereoParams.ortho.skew);*/
                const float modelTranslation = -frustumShift * st->screenZ;

                transformation[0][2] = frustumShift;
                transformation[0][3] = modelTranslation;
            }

            GLmat4x4 newMatrix;
            GPUCMD_SetBufferOffset(o);

            /* multiply original matrix uploaded in flushMatrices with the transformation matrix for this eye */
            multMatrix4x4((float*) bufferMatrixList[i].matrices.projection, (float*) transformation, (float*) newMatrix);

            /* overwrite the right location in the command buffer */
            glUniformMatrix4fv(bufferMatrixList[i].projectionUniform, 1, GL_TRUE, (float*) newMatrix);
        }
    }

    GPUCMD_SetBufferOffset(offset);
}

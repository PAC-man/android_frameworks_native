/*
 * Copyright 2013 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <GLES/gl.h>

#include <utils/String8.h>
#include <cutils/compiler.h>

#include "GLES11RenderEngine.h"
#include "GLExtensions.h"
#include "Mesh.h"

// ---------------------------------------------------------------------------
namespace android {
// ---------------------------------------------------------------------------

GLES11RenderEngine::GLES11RenderEngine() {

    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &mMaxTextureSize);
    glGetIntegerv(GL_MAX_VIEWPORT_DIMS, mMaxViewportDims);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glEnableClientState(GL_VERTEX_ARRAY);
    glShadeModel(GL_FLAT);
    glDisable(GL_DITHER);
    glDisable(GL_CULL_FACE);

    struct pack565 {
        inline uint16_t operator() (int r, int g, int b) const {
            return (r<<11)|(g<<5)|b;
        }
    } pack565;

    const uint16_t protTexData[] = { pack565(0x03, 0x03, 0x03) };
    glGenTextures(1, &mProtectedTexName);
    glBindTexture(GL_TEXTURE_2D, mProtectedTexName);
    glTexParameterx(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameterx(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameterx(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameterx(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0,
            GL_RGB, GL_UNSIGNED_SHORT_5_6_5, protTexData);
}

GLES11RenderEngine::~GLES11RenderEngine() {
}


size_t GLES11RenderEngine::getMaxTextureSize() const {
    return mMaxTextureSize;
}

size_t GLES11RenderEngine::getMaxViewportDims() const {
    return
        mMaxViewportDims[0] < mMaxViewportDims[1] ?
            mMaxViewportDims[0] : mMaxViewportDims[1];
}

void GLES11RenderEngine::setViewportAndProjection(
        size_t vpw, size_t vph, size_t w, size_t h, bool yswap) {
    glViewport(0, 0, vpw, vph);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    // put the origin in the left-bottom corner
    if (yswap)  glOrthof(0, w, h, 0, 0, 1);
    else        glOrthof(0, w, 0, h, 0, 1);
    glMatrixMode(GL_MODELVIEW);
}

void GLES11RenderEngine::setupLayerBlending(
    bool premultipliedAlpha, bool opaque, int alpha) {
    GLenum combineRGB;
    GLenum combineAlpha;
    GLenum src0Alpha;
    GLfloat envColor[4];

    if (CC_UNLIKELY(alpha < 0xFF)) {
        // Cv = premultiplied ? Cs*alpha : Cs
        // Av = !opaque       ? As*alpha : As
        combineRGB   = premultipliedAlpha ? GL_MODULATE : GL_REPLACE;
        combineAlpha = !opaque            ? GL_MODULATE : GL_REPLACE;
        src0Alpha    = GL_CONSTANT;
        envColor[0]  = alpha * (1.0f / 255.0f);
    } else {
        // Cv = Cs
        // Av = opaque ? 1.0 : As
        combineRGB   = GL_REPLACE;
        combineAlpha = GL_REPLACE;
        src0Alpha    = opaque ? GL_CONSTANT : GL_TEXTURE;
        envColor[0]  = 1.0f;
    }

    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
    glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, combineRGB);
    glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_RGB, GL_TEXTURE);
    glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
    if (combineRGB == GL_MODULATE) {
        glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_RGB, GL_CONSTANT);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
    }
    glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, combineAlpha);
    glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_ALPHA, src0Alpha);
    glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
    if (combineAlpha == GL_MODULATE) {
        glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_ALPHA, GL_TEXTURE);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_ALPHA, GL_SRC_ALPHA);
    }
    if (combineRGB == GL_MODULATE || src0Alpha == GL_CONSTANT) {
        envColor[1] = envColor[0];
        envColor[2] = envColor[0];
        envColor[3] = envColor[0];
        glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, envColor);
    }

    if (alpha < 0xFF || !opaque) {
        glEnable(GL_BLEND);
        glBlendFunc(premultipliedAlpha ? GL_ONE : GL_SRC_ALPHA,
                    GL_ONE_MINUS_SRC_ALPHA);
    } else {
        glDisable(GL_BLEND);
    }
}

void GLES11RenderEngine::setupDimLayerBlending(int alpha) {
    glDisable(GL_TEXTURE_EXTERNAL_OES);
    glDisable(GL_TEXTURE_2D);
    if (alpha == 0xFF) {
        glDisable(GL_BLEND);
    } else {
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    }
    glColor4f(0, 0, 0, alpha/255.0f);
}

void GLES11RenderEngine::setupLayerTexturing(size_t textureName,
    bool useFiltering, const float* textureMatrix) {
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, textureName);
    GLenum filter = GL_NEAREST;
    if (useFiltering) {
        filter = GL_LINEAR;
    }
    glTexParameterx(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterx(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameterx(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameterx(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, filter);
    glMatrixMode(GL_TEXTURE);
    glLoadMatrixf(textureMatrix);
    glMatrixMode(GL_MODELVIEW);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_TEXTURE_EXTERNAL_OES);
}

void GLES11RenderEngine::setupLayerBlackedOut() {
    glBindTexture(GL_TEXTURE_2D, mProtectedTexName);
    glMatrixMode(GL_TEXTURE);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glDisable(GL_TEXTURE_EXTERNAL_OES);
    glEnable(GL_TEXTURE_2D);
}

void GLES11RenderEngine::disableTexturing() {
    glDisable(GL_TEXTURE_EXTERNAL_OES);
    glDisable(GL_TEXTURE_2D);
}

void GLES11RenderEngine::disableBlending() {
    glDisable(GL_BLEND);
}

void GLES11RenderEngine::fillWithColor(const Mesh& mesh, float r, float g, float b, float a) {
    glColor4f(r, g, b, a);
    glDisable(GL_TEXTURE_EXTERNAL_OES);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);

    glVertexPointer(mesh.getVertexSize(),
            GL_FLOAT,
            mesh.getByteStride(),
            mesh.getVertices());

    glDrawArrays(mesh.getPrimitive(), 0, mesh.getVertexCount());
}

void GLES11RenderEngine::drawMesh(const Mesh& mesh) {
    if (mesh.getTexCoordsSize()) {
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexCoordPointer(mesh.getTexCoordsSize(),
                GL_FLOAT,
                mesh.getByteStride(),
                mesh.getTexCoords());
    }

    glVertexPointer(mesh.getVertexSize(),
            GL_FLOAT,
            mesh.getByteStride(),
            mesh.getVertices());

    glDrawArrays(mesh.getPrimitive(), 0, mesh.getVertexCount());

    if (mesh.getTexCoordsSize()) {
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    }
}

void GLES11RenderEngine::dump(String8& result) {
    const GLExtensions& extensions(GLExtensions::getInstance());
    result.appendFormat("GLES: %s, %s, %s\n",
            extensions.getVendor(),
            extensions.getRenderer(),
            extensions.getVersion());
    result.appendFormat("%s\n", extensions.getExtension());
}

// ---------------------------------------------------------------------------
}; // namespace android
// ---------------------------------------------------------------------------
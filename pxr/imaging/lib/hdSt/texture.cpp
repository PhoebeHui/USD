//
// Copyright 2016 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//
#include "pxr/imaging/hdSt/texture.h"
#include "pxr/imaging/hdSt/resourceRegistry.h"
#include "pxr/imaging/hdSt/textureResource.h"
#include "pxr/imaging/hdSt/textureResourceHandle.h"

#include "pxr/imaging/hd/changeTracker.h"
#include "pxr/imaging/hd/material.h"
#include "pxr/imaging/hd/sceneDelegate.h"

#include "pxr/imaging/glf/contextCaps.h"

PXR_NAMESPACE_OPEN_SCOPE


HdStTexture::HdStTexture(SdfPath const& id)
  : HdTexture(id)
  , _textureResourceHandle(new HdStTextureResourceHandle())
{
}

HdStTexture::~HdStTexture()
{
}

HdTextureResourceSharedPtr
HdStTexture::_GetTextureResource( HdSceneDelegate *sceneDelegate,
                                  const SdfPath &sceneID,
                                  HdTextureResource::ID texID) const
{
    return sceneDelegate->GetTextureResource(sceneID);
}

namespace {
void
_PropagateTextureDirtinessToMaterials(HdRenderIndex * renderIndex)
{
    HD_TRACE_FUNCTION();

    // XXX: This is a very large hammer - back door hack to propagate
    // that a texture resource has changed to all materials which
    // may be using the texture.
    //
    // This is only necessary when the texture is changed to an incompatible
    // binding, i.e. the texture type has changed (e.g. UV vs Ptex, etc) or
    // has changed to/from defined/undefined. Also, bindless texture handles
    // are still managed by material buffers.
    //
    // This is particularly unpleasant because we are marking material
    // sprims dirty during texture bprim sync, and we must mark all
    // materials dirty. This could be improved if we had a way to identify
    // actual dependencies.
    HdChangeTracker &changeTracker = renderIndex->GetChangeTracker();

    SdfPathVector materials =
                      renderIndex->GetSprimSubtree(HdPrimTypeTokens->material,
                                                   SdfPath::AbsoluteRootPath());

    size_t numMaterials = materials.size();
    for (size_t materialNum = 0; materialNum < numMaterials; ++materialNum) {
        changeTracker.MarkSprimDirty(materials[materialNum],
                                     HdMaterial::DirtyParams);
    }
}
};

void
HdStTexture::_RegisterTextureResource(
        HdRenderIndex *renderIndex,
        const SdfPath &textureHandleId,
        const HdTextureResourceSharedPtr &baseTextureResource)
{
    HdStResourceRegistrySharedPtr const& resourceRegistry =
        boost::static_pointer_cast<HdStResourceRegistry>(
            renderIndex->GetResourceRegistry());

    HdStTextureResourceSharedPtr const& textureResource =
        boost::static_pointer_cast<HdStTextureResource>(baseTextureResource);

    HdResourceRegistry::TextureKey handleKey =
        HdStTextureResourceHandle::GetHandleKey(renderIndex, textureHandleId);
    HdInstance<HdResourceRegistry::TextureKey,
               HdStTextureResourceHandleSharedPtr> handleInstance;

    std::unique_lock<std::mutex> regLock =
    resourceRegistry->RegisterTextureResourceHandle(handleKey,
                                                    &handleInstance);
    if (handleInstance.IsFirstInstance()) {
        handleInstance.SetValue(_textureResourceHandle);
    }

    bool isIncompatibleTexture =
        HdStTextureResourceHandle::IsIncompatibleTextureResource(
            _textureResourceHandle->GetTextureResource(), textureResource);

    _textureResourceHandle->SetTextureResource(textureResource);

    bool bindless = GlfContextCaps::GetInstance().bindlessTextureEnabled;

    if (isIncompatibleTexture || bindless) {
        _PropagateTextureDirtinessToMaterials(renderIndex);
    }
}


PXR_NAMESPACE_CLOSE_SCOPE


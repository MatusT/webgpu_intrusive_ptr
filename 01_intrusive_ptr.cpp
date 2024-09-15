#include "plf_hive.hpp"

#include <boost/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>

#include <print>

/// This file shows minimal example of using boost::intrusive_ptr for reference
/// counting that could be used for implementing WebGPU requirements

// https://www.w3.org/TR/webgpu/#dom-gpubuffer-internal-state-slot
enum class TextureInternalState {
  Available,
  Unavailable,
  Destroyed,
};

// Have some class that can hold Vulkan object to be destroyed
struct TextureToBeDestroyed {
  // vk::Texture texture;
};

// This is now our custom implementation. intrusive_ptr_release relies on
// Resources singleton. It can be made into a template for easy writing just as
// boost::intrusive_ptr
class Texture : public boost::intrusive_ref_counter<Texture> {
public:
  Texture() { std::println("Texture::Constructor"); }

  // Follow: https://www.w3.org/TR/webgpu/#buffer-destruction
  // Notice: no class destructor is called, this doesn't release CPU side
  // object!
  void destroy() {
    if (mInternalState == TextureInternalState::Destroyed) {
      // Valid according to the specification. Nothing to do.
      return;
    }

    // Unmap
    // ...

    // Set state to destroyed
    mInternalState = TextureInternalState::Destroyed;

    // If this was mappable buffer it could have had staging buffer that can be
    // deleted immediately. if (stagingBuffer) delete staging;

    // Enqueue GPU Memory destruction
    // ^ A few design decisions to be made. This can be a lot of things:
    // - some async task running in another thread(s). Here comes handy the
    // thread safety of plf::hive ;-)
    //   Pseudocode:
    //    thread/task_pool::fire_and_forget(() => { wait(texture);
    //    deviceDeleteTexture(texture); });
    // - just a vector that is evaluated at each submit
    //   Pseudocode:
    //     queue.texturesToBeDestroyed.push_back({ .texture = mTexture; });
    // ...
  }

  ~Texture() {
    // Enqueue GPU memory destruction if explicit destroy() call was not made
    if (mInternalState != TextureInternalState::Destroyed) {
      destroy();
    }

    // Continue with destruction of actual CPU object of the implementation
    std::println("Texture::Destructor with count {0}", this->use_count());
  }

protected:
  TextureInternalState mInternalState{TextureInternalState::Available};
};

// This would be somewhere in Instance/Device class or whatever
static plf::hive<Texture> textures;

// This part shows how raw native WebGPU functions can be now implemented using
// above functionality.
#pragma region WebGPU
Texture *wgpuInstanceRequestTexture() {
  boost::intrusive_ptr<Texture> texturePtr{new Texture()};

  // Because WebGPU functions do NOT return intrusive_ptr (that will be
  // destroyed at the end of this function) but raw pointer
  // we artificially increase reference. It will be 2 and 1 after end of this
  // function
  intrusive_ptr_add_ref(texturePtr.get());

  return texturePtr.get();
}

void wgpuTextureDestroy(Texture *texture) { texture->destroy(); }
void wgpuTextureAddRef(Texture *texture) { intrusive_ptr_add_ref(texture); }
void wgpuTextureRelease(Texture *texture) { intrusive_ptr_release(texture); }
#pragma endregion WebGPU

int main() {
  Texture *texture = wgpuInstanceRequestTexture();

  wgpuTextureDestroy(texture);
  wgpuTextureDestroy(texture);
  wgpuTextureDestroy(texture);

  std::println("This is printed before actual release of CPU object.");

  wgpuTextureRelease(texture);
}
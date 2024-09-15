#include "plf_hive.hpp"

#include <boost/intrusive_ptr.hpp>

#include <atomic>
#include <cstdint>
#include <print>

/// This file expands the previous example by storing resources inside a
/// reasonable data structure - plf::hive. Recommended reading:
/// https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2023/p0447r26.html

/// plf::hive has several nice properties for storage
/// - stable pointers this is a trick that can help us
///  1. make maps std::map<resource, state> memory safe!
///  2. implementation of intrusive_ptr_release super easy
/// - compact storage
/// - simultaneous thread-safe creation, access, erasure

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
class Texture {
public:
  Texture(plf::hive<Texture> *hive) : mHive(hive) {
    std::println("Texture::Constructor");
  }

  // This could really be moved anywhere. For example Device would have a
  // "factory function" for creating textures and passing the hive to
  // constructor.
  static boost::intrusive_ptr<Texture> createTexture(plf::hive<Texture> *hive) {
    auto it = hive->emplace(hive);
    auto *p = &(*it);

    return p;
  }

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
    std::println("Texture::Destructor with count {0}", mRefCounter.load());
  }

private:
  plf::hive<Texture> *mHive = nullptr;

protected:
  TextureInternalState mInternalState{TextureInternalState::Available};
  mutable std::atomic<std::uint64_t> mRefCounter{0};

protected:
  friend void intrusive_ptr_add_ref(const Texture *p) noexcept;
  friend void intrusive_ptr_release(const Texture *p) noexcept;
};

inline void intrusive_ptr_add_ref(const Texture *p) noexcept {
  p->mRefCounter++;
}

inline void intrusive_ptr_release(const Texture *p) noexcept {
  if (--(p->mRefCounter) == 0) {
    // Hive(or any other structure) has to be stored inside the resources unless
    // no structures is used and resource is stored randomly in memory. Which
    // may be very unfortunate for cache misses in many situations.
    p->mHive->erase(p->mHive->get_iterator(p));
  }
}

// This would be somewhere in Instance/Device class or whatever
static plf::hive<Texture> textures;

// This part shows how raw native WebGPU functions can be now implemented using
// above functionality.
#pragma region WebGPU
Texture *wgpuInstanceRequestTexture() {
  auto texturePtr = Texture::createTexture(&textures);

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
  wgpuTextureAddRef(texture);

  std::println("the end");
}
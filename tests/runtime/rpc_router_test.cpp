#include "aster/rpc_router.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <span>

#include "test_types.hpp"

namespace {

class StubRpc final : public aster::RpcBackend {
 public:
  aster::Status RegisterClient(const aster::ServiceDescriptor& descriptor) noexcept override {
    registered = descriptor.name;
    return register_status;
  }

  aster::Status RegisterServer(const aster::ServiceDescriptor& descriptor, aster::RawRpcHandler,
                               void*) noexcept override {
    server = descriptor.name;
    return register_status;
  }

  aster::Status CallAsync(const aster::ServiceDescriptor& descriptor, std::span<const std::byte>,
                          std::uint64_t, aster::RawRpcCompletion, void*,
                          const aster::ExecutionContext&) noexcept override {
    called = descriptor.name;
    return call_status;
  }

  aster::Status Seal() noexcept override {
    is_sealed = true;
    return seal_status;
  }

  [[nodiscard]] bool sealed() const noexcept override { return is_sealed; }

  std::string_view registered;
  std::string_view server;
  std::string_view called;
  aster::Status register_status{aster::Status::kOk};
  aster::Status call_status{aster::Status::kOk};
  aster::Status seal_status{aster::Status::kOk};
  bool is_sealed{};
};

void RoutesOnlyConfiguredClients() {
  StubRpc local;
  StubRpc remote;
  aster::RpcRouter<1> router(local);
  const auto descriptor = aster::ServiceTypeSupport<test::AddService>::descriptor();

  assert(router.AddRemoteClient(descriptor, remote) == aster::Status::kOk);
  assert(router.remote_client_count() == 1);
  assert(router.RegisterClient(descriptor) == aster::Status::kOk);
  assert(remote.registered == descriptor.name);
  assert(local.registered.empty());
  assert(router.RegisterServer(descriptor, nullptr, nullptr) == aster::Status::kOk);
  assert(local.server == descriptor.name);
  auto local_descriptor = descriptor;
  local_descriptor.name = "test.v1.Local.Add";
  assert(router.RegisterClient(local_descriptor) == aster::Status::kOk);
  assert(local.registered == local_descriptor.name);
  assert(router.Seal() == aster::Status::kOk);
  assert(local.sealed());
  assert(remote.sealed());

  const std::array request{std::byte{1}};
  const aster::ExecutionContext caller("rpc", aster::ExecutionKind::kThread, 1);
  assert(router.CallAsync(descriptor, request, 0, nullptr, nullptr, caller) == aster::Status::kOk);
  assert(remote.called == descriptor.name);
  assert(local.called.empty());
  assert(router.CallAsync(local_descriptor, request, 0, nullptr, nullptr, caller) ==
         aster::Status::kOk);
  assert(local.called == local_descriptor.name);
}

void RejectsAmbiguousAndMismatchedRoutes() {
  StubRpc local;
  StubRpc remote;
  aster::RpcRouter<1> router(local);
  auto descriptor = aster::ServiceTypeSupport<test::AddService>::descriptor();

  assert(router.AddRemoteClient(descriptor, local) == aster::Status::kInvalidArgument);
  assert(router.AddRemoteClient(descriptor, remote) == aster::Status::kOk);
  assert(router.AddRemoteClient(descriptor, remote) == aster::Status::kAlreadyExists);
  descriptor.response_type.max_serialized_size += 1;
  assert(router.AddRemoteClient(descriptor, remote) == aster::Status::kTypeMismatch);
  assert(router.RegisterClient(descriptor) == aster::Status::kTypeMismatch);
}

}  // namespace

int main() {
  RoutesOnlyConfiguredClients();
  RejectsAmbiguousAndMismatchedRoutes();
  return 0;
}

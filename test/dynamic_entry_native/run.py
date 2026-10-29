"""Execute production entry, host-job admission and cancellation/lifetime code."""
from pathlib import Path
import importlib.util
import subprocess
import os
import sys
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("fixture_checker", ROOT / "test/verify-descriptor-fixture.py")
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)
def body(source, signature): return checker.function_body(source, signature)
def main():
    dynamic = (ROOT / "src/dynamicGatt.cpp").read_text()
    fixture = (ROOT / "src/fixtureGatt.cpp").read_text()
    code = r"""
#include "dynamicGatt.h"
#include <atomic>
#include <mutex>
#include <memory>
#include <cassert>
#include <iostream>
namespace Advertising { unsigned drains=0; void DrainDeferredCallbacks() { ++drains; } }
namespace DynamicGatt {
std::mutex entryMutex;
std::atomic<State> state{State::Ready};
"""
    for signature in ("Entry::Entry(bool requireReady)", "Entry::~Entry()"):
        code += signature + " {\n" + body(dynamic, signature) + "\n}\n"
    code += r"""
}
using SemaphoreHandle_t = int*;
unsigned liveSemaphores=0;
SemaphoreHandle_t xSemaphoreCreateBinary() { ++liveSemaphores; return new int(0); }
void vSemaphoreDelete(SemaphoreHandle_t s) { --liveSemaphores; delete s; }
void xSemaphoreGive(SemaphoreHandle_t s) { *s=1; }
void xSemaphoreTake(SemaphoreHandle_t, int) {}
int pdMS_TO_TICKS(int n) { return n; }
struct ble_npl_event { void (*fn)(ble_npl_event*); void* arg; };
void* ble_npl_event_get_arg(ble_npl_event* e) { return e->arg; }
void ble_npl_event_init(ble_npl_event* e, void (*fn)(ble_npl_event*), void* arg) { e->fn=fn; e->arg=arg; }
ble_npl_event* queued=nullptr;
bool synchronous=false;
void* nimble_port_get_dflt_eventq() { return nullptr; }
void ble_npl_eventq_put(void*, ble_npl_event* e) { if(synchronous) e->fn(e); else { assert(!queued); queued=e; } }
namespace FixtureGatt {
std::atomic<bool> ready{true};
bool Ready() { return ready.load(); }
std::atomic<bool> hostWorkPending{false};
enum class Outcome { Ready,NotReady,Busy,InvalidArgument,Failed };
struct WebResult { Outcome outcome=Outcome::Failed; std::string detail; };
enum class Work { Snapshot, Publish };
struct HostJob {
""" + body(fixture, "struct HostJob") + "\n};\n"
    code += r"""
unsigned executions=0;
WebResult executeHost(const HostJob&) { ++executions; return {Outcome::Ready,"executed"}; }
"""
    for signature in ("void hostCallback(ble_npl_event* event)", "WebResult runHost(Work work, ConnectionId id, const GattValue::Buffer& bytes, size_t length)"):
        code += signature + " {\n" + body(fixture, signature) + "\n}\n"
    code += "bool BeginRebuild() {\n" + body(fixture, "bool BeginRebuild()") + "\n}\n}\n"
    code += r"""
int main() {
    using namespace FixtureGatt;
    {
        DynamicGatt::Entry mutation;
        assert(mutation);
        auto result=runHost(Work::Snapshot,{}, {},0);
        assert(result.outcome==Outcome::NotReady && !queued && !hostWorkPending);
    }
    // A losing readiness/CAS admission cannot enqueue late work after invalidation.
    DynamicGatt::state=DynamicGatt::State::Applying;
    auto blocked=runHost(Work::Snapshot,{}, {},0);
    assert(blocked.outcome==Outcome::NotReady && !queued);
    DynamicGatt::state=DynamicGatt::State::Ready;
    // A GET that wins reservation prevents structural invalidation even after caller timeout.
    auto timed=runHost(Work::Snapshot,{}, {},0);
    assert(timed.outcome==Outcome::Busy && queued && hostWorkPending && liveSemaphores==1);
    {
        DynamicGatt::Entry mutation;
        assert(mutation && !BeginRebuild() && Ready());
    }
    auto second=runHost(Work::Publish,{}, {},0);
    assert(second.outcome==Outcome::Busy && liveSemaphores==1);
    auto* event=queued; queued=nullptr; event->fn(event);
    assert(!hostWorkPending && executions==0 && liveSemaphores==0);
    // The queued owner canceled before execution; no later mutation occurs.
    assert(BeginRebuild() && !Ready());
    ready=true; synchronous=true;
    auto completed=runHost(Work::Snapshot,{}, {},0);
    assert(completed.outcome==Outcome::Ready && executions==1 && !hostWorkPending && liveSemaphores==0);
    DynamicGatt::state=DynamicGatt::State::RecoveryRequired;
    DynamicGatt::Entry quarantined;
    assert(!quarantined);
    std::cout << "PASS: production entry/readiness-CAS, winning GET, queued cancellation and ownership, recovery exclusion\n";
}
"""
    out=ROOT/".pio/dynamic-entry-native"
    out.mkdir(parents=True,exist_ok=True)
    cpp=out/"production-entry.cpp";cpp.write_text(code)
    compiler=ROOT/".pio/native-tools/winlibs-16.2.0-ucrt-r2/mingw64/bin/g++.exe"
    env=os.environ.copy();env["PATH"]=str(compiler.parent)+os.pathsep+env.get("PATH", "")
    binary=out/"entry.exe"
    for label,command in (("compile",[str(compiler),"-std=c++17","-Wall","-Wextra","-Werror","-DBLE_ENABLE_WEB_INTERFACE=1","-I"+str(ROOT/"src"),str(cpp),"-o",str(binary)]),("run",[str(binary)])):
        result=subprocess.run(command,cwd=ROOT,env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        (out/(label+".log")).write_text(result.stdout)
        if result.returncode: raise RuntimeError(result.stdout)
        print(result.stdout,end="")
if __name__ == "__main__": main()

"""Run the production structural transaction and recovery decisions with injected adapters."""
from pathlib import Path
import importlib.util
import subprocess
import os
import sys
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location("checker",ROOT/"test/verify-descriptor-fixture.py")
checker=importlib.util.module_from_spec(spec);spec.loader.exec_module(checker)
def main():
    source=(ROOT/"src/dynamicGatt.cpp").read_text(encoding="utf-8")
    code=r"""
#include "dynamicGatt.h"
#include "gattRebuild.h"
#include <atomic>
#include <cassert>
#include <iostream>
namespace FixtureGatt {
bool ready=true, pending=false, finalizes=true;
bool BeginRebuild() { if(pending||!ready)return false;ready=false;return true; }
bool FinalizeAfterRebuild() { return ready=finalizes; }
}
namespace Advertising {
struct RebuildSnapshot {};
struct StopAllResult { unsigned failed=0; };
bool directed=false,captures=true,restores=true; unsigned stopFailures=0,restorations=0;
bool IsDirectedActive() { return directed; }
bool CaptureForRebuild(RebuildSnapshot&) { return captures; }
StopAllResult StopAll() { assert(!FixtureGatt::ready);return {stopFailures}; }
bool RestoreAfterRebuild(const RebuildSnapshot&) { assert(FixtureGatt::ready);++restorations;return restores; }
}
namespace DynamicGatt {
std::atomic<State> state{State::Ready};
struct Managed { int value=123; };
int failure=0;bool mutationChanges=true;unsigned starts=0,mutations=0,hostCalls=0;
struct NativeOperation {
 bool adding;Managed* target;std::string serviceUuid,characteristicUuid;const GattValue::Buffer& bytes;size_t length;int fixture=0;bool mutated=false,uncertain=false;
 bool idle(){ uncertain=failure==7;return failure!=1&&!uncertain; }
 bool mutate(){++mutations;mutated=mutationChanges;return failure!=2;}
 bool reset(){return failure!=3;}
 void stop(){}
 bool start(){++starts;return failure!=4;}
 bool verify(){FixtureGatt::ready=true;return failure!=5;}
};
template<class F> auto onHost(F fn) { ++hostCalls;return fn(); }
"""
    signature="Result apply(bool adding, Managed* target, std::string serviceUuid, std::string characteristicUuid, const GattValue::Buffer& bytes, size_t length)"
    code+=signature+" {\n"+checker.function_body(source,"Result apply(")+"\n}\n}\n"
    code+=r"""
void reset() {
 using namespace DynamicGatt;state=State::Ready;failure=0;mutationChanges=true;starts=mutations=hostCalls=0;
 FixtureGatt::ready=FixtureGatt::finalizes=true;FixtureGatt::pending=false;
 Advertising::directed=false;Advertising::captures=Advertising::restores=true;Advertising::stopFailures=Advertising::restorations=0;
}
int main() {
 using namespace DynamicGatt;
 Managed target;
 auto run=[&](bool adding=true){return apply(adding,&target,"service","characteristic",{},0);};
 reset(); auto r=run();assert(r.outcome==Outcome::Applied&&state==State::Ready&&FixtureGatt::ready&&starts==1&&Advertising::restorations==1);
 reset();target.value=99;r=run(false);assert(r.outcome==Outcome::Applied&&target.value==123);
 reset();Advertising::restores=false;r=run();assert(r.outcome==Outcome::Applied&&r.errorCode=="advertising_restore_incomplete"&&state==State::Ready);
 reset();Advertising::directed=true;r=run();assert(r.outcome==Outcome::Busy&&!hostCalls&&FixtureGatt::ready);
 reset();FixtureGatt::pending=true;r=run();assert(r.outcome==Outcome::Busy&&!hostCalls&&FixtureGatt::ready);
 reset();failure=1;r=run();assert(r.outcome==Outcome::Failed&&state==State::Ready&&FixtureGatt::ready&&!mutations&&!starts);
 reset();mutationChanges=false;failure=2;r=run();assert(r.outcome==Outcome::Failed&&state==State::Ready&&!starts);
 for(int f:{2,3,4,5,7}) {
  reset();failure=f;r=run();assert(r.outcome==Outcome::Failed&&r.errorCode=="gatt_recovery_required"&&state==State::RecoveryRequired&&!FixtureGatt::ready&&!Advertising::restorations);
  assert(r.detail.find("Restart the device:")==0);
  if(f<4||f==7)assert(!starts);
 }
 reset();Advertising::stopFailures=1;r=run();assert(r.outcome==Outcome::Failed&&!hostCalls&&state==State::Ready);
 reset();failure=1;FixtureGatt::finalizes=false;r=run();assert(r.errorCode=="gatt_recovery_required"&&state==State::RecoveryRequired);
 std::cout<<"PASS: production transaction success, unchanged rejection, uncertain/native failures, quarantine, partial restoration\n";
}
"""
    out=ROOT/".pio/dynamic-recovery-native";out.mkdir(parents=True,exist_ok=True)
    cpp=out/"transaction.cpp";cpp.write_text(code,encoding="utf-8")
    compiler=ROOT/".pio/native-tools/winlibs-16.2.0-ucrt-r2/mingw64/bin/g++.exe"
    env=os.environ.copy();env["PATH"]=str(compiler.parent)+os.pathsep+env.get("PATH", "")
    binary=out/"transaction.exe"
    for label,command in (("compile",[str(compiler),"-std=c++17","-Wall","-Wextra","-Werror","-I"+str(ROOT/"src"),str(cpp),"-o",str(binary)]),("run",[str(binary)])):
        result=subprocess.run(command,cwd=ROOT,env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        (out/(label+".log")).write_text(result.stdout,encoding="utf-8")
        if result.returncode:raise RuntimeError(result.stdout)
        print(result.stdout,end="")
if __name__=="__main__":main()

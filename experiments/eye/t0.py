from eyelib import *
print("유형/쪽 예:", len(sel("R1-6","left")), len(sel("LC9","left")))
for label,stim in [("자극 없음",{}),
                   ("LC9 왼 150Hz(지금 방식)",{i:150 for i in sel("LC9","left")}),
                   ("R1-6 왼 20Hz",{i:20 for i in sel("R1-6","left")}),
                   ("R1-6 왼 100Hz",{i:100 for i in sel("R1-6","left")}),
                   ("L1,L2 왼 50Hz",{i:50 for i in sel(["L1","L2"],"left")}),
                   ("Mi1,Tm1,Tm2,Tm3 왼 50Hz",{i:50 for i in sel(["Mi1","Tm1","Tm2","Tm3"],"left")}),
                   ("T2,TmY3,Tm4,Tm2,Tm3 왼 50Hz(LC4 입력)",{i:50 for i in sel(["T2","TmY3","Tm4","Tm2","Tm3"],"left")}),
                   ("Tm5f,T2a 왼 50Hz(LC9 입력)",{i:50 for i in sel(["Tm5f","T2a"],"left")}),
                   ]:
    r,info=run(stim,ms=1000)
    print(f"\n[{label}] {info}\n  (왼/오) {table(r)}")

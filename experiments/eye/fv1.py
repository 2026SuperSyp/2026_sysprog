# flyvis(학습된 망)에 자극을 넣고 유형별·칸별 활동을 저장한다
import sys, torch, numpy as np, logging
logging.disable(logging.CRITICAL)
from flyvis import NetworkView
from flyvis.utils.hex_utils import get_hex_coords, hex_to_pixel
model = sys.argv[1] if len(sys.argv) > 1 else "000"
nv = NetworkView(f"flow/0000/{model}"); net = nv.init_network()
u, v = get_hex_coords(15); x, y = hex_to_pixel(u, v); x = np.asarray(x, float); y = np.asarray(y, float)
dt = 1/100; T = 100                      # 1초
t = np.arange(T) * dt
BG, DARK = 0.5, 0.0
def disk(cx, cy, r):                     # 프레임마다 (cx, cy, r)
    f = np.full((T, 721), BG, np.float32)
    for k in range(T):
        f[k, (x - cx[k])**2 + (y - cy[k])**2 <= r[k]**2] = DARK
    return f
one = np.ones(T)
span = np.abs(x).max()
stims = {
 "gray":        np.full((T, 721), BG, np.float32),
 "small_move":  disk(-0.6*span + 1.2*span*t, 0*one, 1.5*one),      # 작은 물체가 가로로 지나감
 "mid_move":    disk(-0.6*span + 1.2*span*t, 0*one, 4.0*one),
 "big_move":    disk(-0.6*span + 1.2*span*t, 0*one, 8.0*one),
 "small_still": disk(0*one, 0*one, 1.5*one),
 "loom":        disk(0*one, 0*one, np.minimum(1.0 + 26.0*t, 14.0)),  # 0.5초 동안 커짐
 "flicker":     np.where((np.floor(t*10) % 2 == 0)[:, None], BG, 0.15).astype(np.float32) * np.ones((1, 721), np.float32),
}
ct = [str(c) for c in net.connectome.unique_cell_types[:].astype(str)]
out = {"u": u, "v": v, "types": np.array(ct)}
with torch.no_grad():
    for name, f in stims.items():
        movie = torch.tensor(f)[None, :, None, :]                 # (표본, 프레임, 1, 칸)
        gray = torch.full((1, 1, 721), BG)
        init = net.fade_in_state(1.0, dt, gray)                    # 회색 화면에 1초 적응시킨 상태에서 시작
        act = net.simulate(movie, dt, initial_state=init, as_layer_activity=True)
        for c in ct:
            a = act[c].cpu().numpy()[0]                            # (프레임, 칸)
            out[f"{name}|{c}"] = a.astype(np.float32)
        print(name, "T4a", float(act["T4a"].abs().max()), "T2", float(act["T2"].max()), act["T2"].shape)
np.savez_compressed(f"flyvis_act_{model}.npz", **out)

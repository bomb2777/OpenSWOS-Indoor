#!/usr/bin/env python3
"""fake_ooo_api.py -- (OpenSWOS-Indoor) a stand-in for SWOS United's OOO API, for testing the
ranking desk (Server/RankDesk.cpp) and the game's provider (src/indoor_ranking.c) without the
real site: plain HTTP on 127.0.0.1, the endpoints and answers of its API document as far as the
game uses them (auth/key, swos-matches/create, report, add-leg, close, open-matches).

    scripts/fake_ooo_api.py [port]          (default 18080; users: any name, password "pw")
    swos-matchmaker ... --rank-api http://127.0.0.1:18080

Every request is printed (password and key masked) and appended to $FAKE_OOO_LOG if set, so a
test can check what arrived.
"""
import json, os, secrets, sys, threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

LOCK = threading.Lock()
KEYS = {}            # api_key -> username (upper case: the site's names compare without case)
MATCHES = {}         # match_id -> {players: [a, b], ranking, legs: {leg_id: {reports}}, closed}
NEXT = {"match": 15900, "leg": 30200}
LOG = os.environ.get("FAKE_OOO_LOG")


def new_id(kind):
    NEXT[kind] += 1
    return NEXT[kind]


def answer(handler, code, obj):
    body = json.dumps(obj).encode()
    handler.send_response(code)
    handler.send_header("Content-Type", "application/json")
    handler.send_header("Content-Length", str(len(body)))
    handler.end_headers()
    handler.wfile.write(body)


def open_legs(m):
    return [lid for lid, leg in m["legs"].items() if not leg.get("applied")]


class Api(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0))
        try:
            req = json.loads(self.rfile.read(n) or b"{}")
        except ValueError:
            return answer(self, 400, {"status": "bad_json"})
        shown = {k: ("***" if k in ("password", "api_key") else v) for k, v in req.items()}
        line = json.dumps({"path": self.path, "req": shown})
        print(line, flush=True)
        if LOG:
            with open(LOG, "a") as f:
                f.write(line + "\n")
        with LOCK:
            return self.handle_api(req)

    def handle_api(self, req):
        if self.path == "/api/auth/key":
            if not req.get("username") or not req.get("password"):
                return answer(self, 400, {"status": "missing_credentials"})
            if req["password"] != "pw":
                return answer(self, 401, {"status": "invalid_credentials"})
            user = req["username"].upper()
            for k in [k for k, u in KEYS.items() if u == user]:
                del KEYS[k]                     # a new key invalidates the old one
            key = secrets.token_hex(32)
            KEYS[key] = user
            return answer(self, 200, {"status": "ok", "user_id": 800 + len(KEYS), "api_key": key,
                                      "streamkey": "SWOS++/x"})
        user = KEYS.get(req.get("api_key", ""))
        if not user:
            return answer(self, 401, {"status": "invalid_api_key"})
        if self.path == "/api/swos-matches/create":
            opp = (req.get("opponent_username") or "").upper()
            if not req.get("ranking_id") or not opp:
                return answer(self, 400, {"status": "missing_fields"})
            if opp == user:
                return answer(self, 400, {"status": "invalid_opponent"})
            for mid, m in MATCHES.items():
                if not m["closed"] and set(m["players"]) == {user, opp} and m["ranking"] == req["ranking_id"]:
                    added = 0
                    if not open_legs(m):
                        m["legs"][new_id("leg")] = {"reports": {}}
                        added = 1
                    return answer(self, 200, {"status": "ok", "match_id": mid, "leg_ids": list(m["legs"]),
                                              "open_leg_ids": open_legs(m), "created": False, "legs_added": added})
            mid, lid = new_id("match"), new_id("leg")
            MATCHES[mid] = {"players": [user, opp], "ranking": req["ranking_id"], "legs": {lid: {"reports": {}}},
                            "closed": False}
            return answer(self, 200, {"status": "ok", "match_id": mid, "leg_ids": [lid], "open_leg_ids": [lid],
                                      "created": True, "legs_added": 1})
        if self.path == "/api/swos-matches/report":
            lid = req.get("leg_id")
            if lid is None:
                return answer(self, 400, {"status": "missing_leg_id"})
            if "score_own" not in req or "score_opponent" not in req:
                return answer(self, 400, {"status": "missing_score"})
            for m in MATCHES.values():
                if lid in m["legs"]:
                    if user not in m["players"]:
                        return answer(self, 403, {"status": "not_participant"})
                    leg = m["legs"][lid]
                    if leg.get("applied"):
                        return answer(self, 200, {"status": "not_found"})
                    # from the home player's (players[0]) side
                    own = (req["score_own"], req.get("extra_time_own"), req.get("penalties_own"))
                    opp = (req["score_opponent"], req.get("extra_time_opponent"), req.get("penalties_opponent"))
                    home = own + opp if user == m["players"][0] else opp + own
                    leg["reports"][user] = home
                    if len(leg["reports"]) < 2:
                        return answer(self, 200, {"status": "waiting"})
                    a, b = leg["reports"].values()
                    if a != b:
                        return answer(self, 200, {"status": "conflict"})
                    leg["applied"] = True
                    return answer(self, 200, {"status": "applied"})
            return answer(self, 404, {"status": "not_found"})
        if self.path in ("/api/swos-matches/add-leg", "/api/swos-matches/close"):
            mid = req.get("match_id")
            if mid is None:
                return answer(self, 400, {"status": "missing_match_id"})
            m = MATCHES.get(mid)
            if not m:
                return answer(self, 404, {"status": "not_found"})
            if user not in m["players"]:
                return answer(self, 403, {"status": "not_participant"})
            if self.path.endswith("add-leg"):
                if m["closed"]:
                    return answer(self, 400, {"status": "session_closed"})
                lid = new_id("leg")
                m["legs"][lid] = {"reports": {}}
                return answer(self, 200, {"status": "ok", "match_id": mid, "leg_id": lid, "created": True,
                                          "open_leg_ids": open_legs(m)})
            if m["closed"]:
                return answer(self, 200, {"status": "already_closed", "match_id": mid})
            m["closed"] = True
            pending = sum(1 for leg in m["legs"].values() if len(leg["reports"]) == 1)
            return answer(self, 200, {"status": "closing" if pending else "closed", "match_id": mid,
                                      "pending_legs": pending})
        if self.path == "/api/open-matches":
            legs = [{"match_id": mid, "leg_id": lid, "opponent_username": [p for p in m["players"] if p != user][0]}
                    for mid, m in MATCHES.items() if user in m["players"] and not m["closed"]
                    for lid in open_legs(m)]
            return answer(self, 200, {"status": "ok", "ooo_matches": legs, "open_sessions": [], "league_matches": []})
        return answer(self, 404, {"status": "no_such_endpoint"})


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 18080
    print(f"fake OOO API on http://127.0.0.1:{port}", flush=True)
    ThreadingHTTPServer(("127.0.0.1", port), Api).serve_forever()

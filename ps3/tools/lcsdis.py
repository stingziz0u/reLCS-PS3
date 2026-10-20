#!/usr/bin/env python3
# A disassembler for reLCS's main.scm (LCS PS2 format, as res reads it):
# opcodes and argument kinds from src/control/ScriptDebug.cpp's command table.
import re, struct, sys

NUM_TIMERS, NUM_LOCAL_VARS, NUM_GLOBAL_SLOTS = 2, 96, 26
A_TIMER = 10
A_LOCAL = A_TIMER + NUM_TIMERS
A_LOCAL_ARRAY = A_LOCAL + NUM_LOCAL_VARS
A_GLOBAL = A_LOCAL_ARRAY + NUM_LOCAL_VARS
A_GLOBAL_ARRAY = A_GLOBAL + NUM_GLOBAL_SLOTS

def load_table(src):
    enum = open(f'{src}/control/ScriptCommands.h').read()
    enum = re.sub(r'#ifdef SUPPORT_GINPUT_SCRIPT\n.*?#else\n(.*?)#endif\n', r'\1', enum, flags=re.S)
    names = {}; n = 0
    for m in re.finditer(r'^\s*(COMMAND_\w+)\s*(?:=\s*(\d+))?\s*,', enum, re.M):
        if m.group(2): n = int(m.group(2))
        names[m.group(1)] = n; n += 1
    tab = {}
    dbg = open(f'{src}/control/ScriptDebug.cpp').read()
    for m in re.finditer(r'REGISTER_COMMAND\((COMMAND_\w+),\s*INPUT_ARGUMENTS\(([^)]*)\),\s*OUTPUT_ARGUMENTS\(([^)]*)\)', dbg):
        ins = [a.strip() for a in m.group(2).split(',') if a.strip()]
        outs = [a.strip() for a in m.group(3).split(',') if a.strip()]
        if m.group(1) in names:
            tab[names[m.group(1)]] = (m.group(1)[8:], ins, outs)
    # commands the debug table lists without arguments: the counts their
    # handlers collect (CollectParameters / StoreParameters with a number)
    code = ''
    import glob
    for f in sorted(glob.glob(f'{src}/control/Script*.cpp')):
        if 'ScriptDebug' not in f:
            code += open(f).read()
    blocks = re.split(r'\n\s*case (COMMAND_\w+):', code)
    pend = []
    for name, body in zip(blocks[1::2], blocks[2::2]):
        pend.append(name)
        if not re.search(r'\S', body.split('\n', 1)[0]) and body.strip().startswith('case'):
            continue
        body = body.split('\n\tcase ')[0]
        coll = sum(int(x) for x in re.findall(r'CollectParameters\(&m_nIp,\s*(\d+)\)', body))
        store = sum(int(x) for x in re.findall(r'StoreParameters\(&m_nIp,\s*(\d+)\)', body))
        for nm in pend:
            if nm not in names or names[nm] not in tab:
                continue
            n, ins, outs = tab[names[nm]]
            if ins and ins[0] == 'ARGTYPE_FUNCTION':
                continue
            nstr = len(re.findall(r'\+=\s*KEY_LENGTH_IN_SCRIPT|GetTextByKeyFromScript\(&m_nIp', body))
            if nstr and 'ARGTYPE_STRING' not in ins and not ins:
                # the order of the handler: labels and parameters as it reads them
                seq = []
                for m in re.finditer(r'\+=\s*KEY_LENGTH_IN_SCRIPT|GetTextByKeyFromScript\(&m_nIp|CollectParameters\(&m_nIp,\s*(\d+)\)|StoreParameters\(&m_nIp,\s*(\d+)\)', body):
                    if m.group(1): seq += ['ARGTYPE_INT'] * int(m.group(1))
                    elif m.group(2): seq += ['ARGTYPE_INT'] * int(m.group(2))
                    else: seq.append('ARGTYPE_STRING')
                tab[names[nm]] = (n, seq, [])
                continue
            nplain = len([t for t in ins if t != 'ARGTYPE_STRING'])
            if coll > nplain and 'ARGTYPE_STRING' not in ins:
                ins = ['ARGTYPE_INT'] * coll
            if store and not outs:
                outs = ['ARGTYPE_INT'] * store
            tab[names[nm]] = (n, ins, outs)
        pend = []
    return tab

class Dis:
    def __init__(self, d, tab):
        self.d = d; self.tab = tab
    def u8(self, ip): return self.d[ip]
    def arg(self, ip):
        t = self.d[ip]; ip += 1
        if t == 0: return ip, ('end', None)
        if t in (1, 2): return ip, ('i', 0)
        if t == 3: return ip+1, ('f', struct.unpack('>f', bytes([self.d[ip], 0, 0, 0]))[0])
        if t == 4:
            v = struct.unpack_from('<H', self.d, ip)[0] << 16
            return ip+2, ('f', struct.unpack('<f', struct.pack('<I', v))[0])
        if t == 5:
            v = self.d[ip] << 8 | struct.unpack_from('<H', self.d, ip+1)[0] << 16
            return ip+3, ('f', struct.unpack('<f', struct.pack('<I', v))[0])
        if t == 6: return ip+4, ('i', struct.unpack_from('<i', self.d, ip)[0])
        if t == 7: return ip+1, ('i', struct.unpack_from('<b', self.d, ip)[0])
        if t == 8: return ip+2, ('i', struct.unpack_from('<h', self.d, ip)[0])
        if t == 9: return ip+4, ('f', struct.unpack_from('<f', self.d, ip)[0])
        if t >= A_GLOBAL_ARRAY:
            return ip+3, ('ga', ((t - A_GLOBAL_ARRAY) << 8) + self.d[ip])
        if t >= A_GLOBAL:
            return ip+1, ('g', ((t - A_GLOBAL) << 8) + self.d[ip])
        if t >= A_LOCAL_ARRAY:
            return ip+2, ('la', t - A_LOCAL_ARRAY)
        if t >= A_LOCAL:
            return ip, ('l', t - A_LOCAL)
        return ip, ('t', t - A_TIMER)
    def insn(self, ip):
        start = ip
        raw = struct.unpack_from('<H', self.d, ip)[0]; ip += 2
        op = raw & 0x7fff; neg = bool(raw & 0x8000)
        if op not in self.tab:
            raise ValueError(f'unknown opcode {op:04x} at {start}')
        name, ins, outs = self.tab[op]
        args = []
        if ins and ins[0] == 'ARGTYPE_FUNCTION':
            nin, nout, nloc = self.d[ip], self.d[ip+1], self.d[ip+2]; ip += 3
            ip, a = self.arg(ip); args.append(a)
            for i in range(nin + nout):	# the outputs follow (stored on RETURN)
                ip, a = self.arg(ip); args.append(a)
            return ip, (start, op, neg, name, args)
        for t in ins + outs:
            if t == 'ARGTYPE_STRING':
                args.append(('s', self.d[ip:ip+8].split(b'\0')[0].decode('latin1'))); ip += 8
            else:
                ip, a = self.arg(ip); args.append(a)
        if name == 'START_NEW_SCRIPT':
            while True:
                ip, a = self.arg(ip)
                if a[0] == 'end': break
                args.append(a)
        return ip, (start, op, neg, name, args)

def fmt(a):
    k, v = a
    if k == 'g': return f'$g{v}'
    if k == 'ga': return f'$g{v}[]'
    if k == 'l': return f'{v}@'
    if k == 'la': return f'{v}@[]'
    if k == 't': return f'timer{v}'
    if k == 'f': return f'{v:.3f}'
    if k == 's': return f"'{v}'"
    return str(v)

def main():
    data = open(sys.argv[1], 'rb').read()
    src = sys.argv[2]
    main_size = struct.unpack_from('<I', data, 0)[0]
    code = data[8:8+main_size]
    d = Dis(code, load_table(src))
    ip = 0
    # header: GOTOs over the data blocks
    while True:
        nip, ins = d.insn(ip)
        if ins[3] != 'GOTO': break
        tgt = ins[4][0][1]
        print(f'; header GOTO at {ip} -> {tgt}')
        ip = tgt
    while ip < main_size:
        try:
            nip, (start, op, neg, name, args) = d.insn(ip)
        except Exception as e:
            print(f'; stop: {e}'); break
        print(f'{start:7d}: {"NOT " if neg else ""}{name} ' + ' '.join(fmt(a) for a in args))
        ip = nip

if __name__ == '__main__':
    main()

def HammingCodeLength(n):
# HammingCode長度需包含HammingCode(放在2的冪次方和原碼)
    for k in range(n):
        if(2**k >= k+n+1):
            return k
        
def HammingTrans(code, r):
    code.reverse()
    j=0
    codePos=0
    arr=[]
    for i in range(r+len(code)):
        if (i+1==2**j):
        # hamming code位置
            arr.append(0)
            j+=1
        else:
        # 原碼位置
            arr.append(int(code[codePos]))
            codePos+=1
    j=0
    for parity in range(0, len(arr)):
    # XOR計算Hamming code
        if (parity+1==2**j):
            Xor=[]
            Starti=2**j-1
            index=Starti

            while (index<len(arr)):
                block=arr[index:index+2**j]
                Xor.extend(block)
                index+=2**(j+1)
            for z in range(1, len(Xor)):
                arr[Starti]=arr[Starti]^Xor[z]
            j+=1
    arr.reverse()

    return arr

def detect(code):
    code.reverse()
    arr=[]
    arr_copy=[]
    parity_list=[]
    error=0
    for k in range (0, len(code)):
        arr.append(int(code[k]))
        arr_copy.append(int(code[k]))

    j=0
    for pariety in range (0, len(arr)):
    # 計算pariety(hamming code)值
        if (pariety+1==2**j):
            Xor=[]
            Starti=2**j-1
            index=Starti

            while (index<len(arr)):
                block=arr[index:index+2**j]
                Xor.extend(block)
                index+=2**(j+1)
            for z in range(1, len(Xor)):
                arr[Starti]=arr[Starti]^Xor[z]
            parity_list.append(arr[pariety])
            j+=1
    parity_list.reverse()
    # 錯誤位置
    error=sum(int(parity_list) * (2 ** index) for index, parity_list in enumerate(parity_list[::-1]))
    return arr_copy, error
    
#編碼
code='10101111010'
encode=list(code)
totalLength=HammingCodeLength(len(encode))
Hammingcode=HammingTrans(encode, totalLength)
print(int(''.join(map(str, Hammingcode))))

#解碼
coded='101011111010100'
decode=list(coded)
HammingCor, error=detect(decode)
if (error==0):
    print('No error')
else:
    print('The position of error is', len(decode)-error+1)
    if (HammingCor[error-1]=='0'):
        HammingCor[error-1]='1'
    else:
        HammingCor[error-1]='0'
    HammingCor.reverse()
    print(int(''.join(map(str, HammingCor))))
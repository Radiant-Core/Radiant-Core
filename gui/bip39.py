#!/usr/bin/env python3
"""
BIP39 Mnemonic implementation for Radiant Node GUI.
Generates and validates 12/15/18/21/24-word seed phrases for wallet backup/restore.
Supports BIP44 key derivation (m/44'/512'/0'/0/k, Radiant SLIP-0044 coin type)
for wallet generation.
"""

import hashlib
import hmac
import secrets
import struct

# BIP39 English wordlist (2048 words) - standard wordlist
WORDLIST = """abandon ability able about above absent absorb abstract absurd abuse
access accident account accuse achieve acid acoustic acquire across act
action actor actress actual adapt add addict address adjust admit
adult advance advice aerobic affair afford afraid again age agent
agree ahead aim air airport aisle alarm album alcohol alert
alien all alley allow almost alone alpha already also alter
always amateur amazing among amount amused analyst anchor ancient anger
angle angry animal ankle announce annual another answer antenna antique
anxiety any apart apology appear apple approve april arch arctic
area arena argue arm armed armor army around arrange arrest
arrive arrow art artefact artist artwork ask aspect assault asset
assist assume asthma athlete atom attack attend attitude attract auction
audit august aunt author auto autumn average avocado avoid awake
aware away awesome awful awkward axis baby bachelor bacon badge
bag balance balcony ball bamboo banana banner bar barely bargain
barrel base basic basket battle beach bean beauty because become
beef before begin behave behind believe below belt bench benefit
best betray better between beyond bicycle bid bike bind biology
bird birth bitter black blade blame blanket blast bleak bless
blind blood blossom blouse blue blur blush board boat body
boil bomb bone bonus book boost border boring borrow boss
bottom bounce box boy bracket brain brand brass brave bread
breeze brick bridge brief bright bring brisk broccoli broken bronze
broom brother brown brush bubble buddy budget buffalo build bulb
bulk bullet bundle bunker burden burger burst bus business busy
butter buyer buzz cabbage cabin cable cactus cage cake call
calm camera camp can canal cancel candy cannon canoe canvas
canyon capable capital captain car carbon card cargo carpet carry
cart case cash casino castle casual cat catalog catch category
cattle caught cause caution cave ceiling celery cement census century
cereal certain chair chalk champion change chaos chapter charge chase
chat cheap check cheese chef cherry chest chicken chief child
chimney choice choose chronic chuckle chunk churn cigar cinnamon circle
citizen city civil claim clap clarify claw clay clean clerk
clever click client cliff climb clinic clip clock clog close
cloth cloud clown club clump cluster clutch coach coast coconut
code coffee coil coin collect color column combine come comfort
comic common company concert conduct confirm congress connect consider control
convince cook cool copper copy coral core corn correct cost
cotton couch country couple course cousin cover coyote crack cradle
craft cram crane crash crater crawl crazy cream credit creek
crew cricket crime crisp critic crop cross crouch crowd crucial
cruel cruise crumble crunch crush cry crystal cube culture cup
cupboard curious current curtain curve cushion custom cute cycle dad
damage damp dance danger daring dash daughter dawn day deal
debate debris decade december decide decline decorate decrease deer defense
define defy degree delay deliver demand demise denial dentist deny
depart depend deposit depth deputy derive describe desert design desk
despair destroy detail detect develop device devote diagram dial diamond
diary dice diesel diet differ digital dignity dilemma dinner dinosaur
direct dirt disagree discover disease dish dismiss disorder display distance
divert divide divorce dizzy doctor document dog doll dolphin domain
donate donkey donor door dose double dove draft dragon drama
drastic draw dream dress drift drill drink drip drive drop
drum dry duck dumb dune during dust dutch duty dwarf
dynamic eager eagle early earn earth easily east easy echo
ecology economy edge edit educate effort egg eight either elbow
elder electric elegant element elephant elevator elite else embark embody
embrace emerge emotion employ empower empty enable enact end endless
endorse enemy energy enforce engage engine enhance enjoy enlist enough
enrich enroll ensure enter entire entry envelope episode equal equip
era erase erode erosion error erupt escape essay essence estate
eternal ethics evidence evil evoke evolve exact example excess exchange
excite exclude excuse execute exercise exhaust exhibit exile exist exit
exotic expand expect expire explain expose express extend extra eye
eyebrow fabric face faculty fade faint faith fall false fame
family famous fan fancy fantasy farm fashion fat fatal father
fatigue fault favorite feature february federal fee feed feel female
fence festival fetch fever few fiber fiction field figure file
film filter final find fine finger finish fire firm first
fiscal fish fit fitness fix flag flame flash flat flavor
flee flight flip float flock floor flower fluid flush fly
foam focus fog foil fold follow food foot force forest
forget fork fortune forum forward fossil foster found fox fragile
frame frequent fresh friend fringe frog front frost frown frozen
fruit fuel fun funny furnace fury future gadget gain galaxy
gallery game gap garage garbage garden garlic garment gas gasp
gate gather gauge gaze general genius genre gentle genuine gesture
ghost giant gift giggle ginger giraffe girl give glad glance
glare glass glide glimpse globe gloom glory glove glow glue
goat goddess gold good goose gorilla gospel gossip govern gown
grab grace grain grant grape grass gravity great green grid
grief grit grocery group grow grunt guard guess guide guilt
guitar gun gym habit hair half hammer hamster hand happy
harbor hard harsh harvest hat have hawk hazard head health
heart heavy hedgehog height hello helmet help hen hero hidden
high hill hint hip hire history hobby hockey hold hole
holiday hollow home honey hood hope horn horror horse hospital
host hotel hour hover hub huge human humble humor hundred
hungry hunt hurdle hurry hurt husband hybrid ice icon idea
identify idle ignore ill illegal illness image imitate immense immune
impact impose improve impulse inch include income increase index indicate
indoor industry infant inflict inform inhale inherit initial inject injury
inmate inner innocent input inquiry insane insect inside inspire install
intact interest into invest invite involve iron island isolate issue
item ivory jacket jaguar jar jazz jealous jeans jelly jewel
job join joke journey joy judge juice jump jungle junior
junk just kangaroo keen keep ketchup key kick kid kidney
kind kingdom kiss kit kitchen kite kitten kiwi knee knife
knock know lab label labor ladder lady lake lamp language
laptop large later latin laugh laundry lava law lawn lawsuit
layer lazy leader leaf learn leave lecture left leg legal
legend leisure lemon lend length lens leopard lesson letter level
liar liberty library license life lift light like limb limit
link lion liquid list little live lizard load loan lobster
local lock logic lonely long loop lottery loud lounge love
loyal lucky luggage lumber lunar lunch luxury lyrics machine mad
magic magnet maid mail main major make mammal man manage
mandate mango mansion manual maple marble march margin marine market
marriage mask mass master match material math matrix matter maximum
maze meadow mean measure meat mechanic medal media melody melt
member memory mention menu mercy merge merit merry mesh message
metal method middle midnight milk million mimic mind minimum minor
minute miracle mirror misery miss mistake mix mixed mixture mobile
model modify mom moment monitor monkey monster month moon moral
more morning mosquito mother motion motor mountain mouse move movie
much muffin mule multiply muscle museum mushroom music must mutual
myself mystery myth naive name napkin narrow nasty nation nature
near neck need negative neglect neither nephew nerve nest net
network neutral never news next nice night noble noise nominee
noodle normal north nose notable nothing notice novel now nuclear
number nurse nut oak obey object oblige obscure observe obtain
obvious occur ocean october odor off offer office often oil
okay old olive olympic omit once one onion online only
open opera opinion oppose option orange orbit orchard order ordinary
organ orient original orphan ostrich other outdoor outer output outside
oval oven over own owner oxygen oyster ozone pact paddle
page pair palace palm panda panel panic panther paper parade
parent park parrot party pass patch path patient patrol pattern
pause pave payment peace peanut pear peasant pelican pen penalty
pencil people pepper perfect permit person pet phone photo phrase
physical piano picnic picture piece pig pigeon pill pilot pink
pioneer pipe pistol pitch pizza place planet plastic plate play
please pledge pluck plug plunge poem poet point polar pole
police pond pony pool popular portion position possible post potato
pottery poverty powder power practice praise predict prefer prepare present
pretty prevent price pride primary print priority prison private prize
problem process produce profit program project promote proof property prosper
protect proud provide public pudding pull pulp pulse pumpkin punch
pupil puppy purchase purity purpose purse push put puzzle pyramid
quality quantum quarter question quick quit quiz quote rabbit raccoon
race rack radar radio rail rain raise rally ramp ranch
random range rapid rare rate rather raven raw razor ready
real reason rebel rebuild recall receive recipe record recycle reduce
reflect reform refuse region regret regular reject relax release relief
rely remain remember remind remove render renew rent reopen repair
repeat replace report require rescue resemble resist resource response result
retire retreat return reunion reveal review reward rhythm rib ribbon
rice rich ride ridge rifle right rigid ring riot ripple
risk ritual rival river road roast robot robust rocket romance
roof rookie room rose rotate rough round route royal rubber
rude rug rule run runway rural sad saddle sadness safe
sail salad salmon salon salt salute same sample sand satisfy
satoshi sauce sausage save say scale scan scare scatter scene
scheme school science scissors scorpion scout scrap screen script scrub
sea search season seat second secret section security seed seek
segment select sell seminar senior sense sentence series service session
settle setup seven shadow shaft shallow share shed shell sheriff
shield shift shine ship shiver shock shoe shoot shop short
shoulder shove shrimp shrug shuffle shy sibling sick side siege
sight sign silent silk silly silver similar simple since sing
siren sister situate six size skate sketch ski skill skin
skirt skull slab slam sleep slender slice slide slight slim
slogan slot slow slush small smart smile smoke smooth snack
snake snap sniff snow soap soccer social sock soda soft
solar soldier solid solution solve someone song soon sorry sort
soul sound soup source south space spare spatial spawn speak
special speed spell spend sphere spice spider spike spin spirit
split spoil sponsor spoon sport spot spray spread spring spy
square squeeze squirrel stable stadium staff stage stairs stamp stand
start state stay steak steel stem step stereo stick still
sting stock stomach stone stool story stove strategy street strike
strong struggle student stuff stumble style subject submit subway success
such sudden suffer sugar suggest suit summer sun sunny sunrise
sunset super supply supreme sure surface surge surprise surround survey
suspect sustain swallow swamp swap swarm swear sweet swift swim
swing switch sword symbol symptom syrup system table tackle tag
tail talent talk tank tape target task taste tattoo taxi
teach team tell ten tenant tennis tent term test text
thank that theme then theory there they thing this thought
three thrive throw thumb thunder ticket tide tiger tilt timber
time tiny tip tired tissue title toast tobacco today toddler
toe together toilet token tomato tomorrow tone tongue tonight tool
tooth top topic topple torch tornado tortoise toss total tourist
toward tower town toy track trade traffic tragic train transfer
trap trash travel tray treat tree trend trial tribe trick
trigger trim trip trophy trouble truck true truly trumpet trust
truth try tube tuition tumble tuna tunnel turkey turn turtle
twelve twenty twice twin twist two type typical ugly umbrella
unable unaware uncle uncover under undo unfair unfold unhappy uniform
unique unit universe unknown unlock until unusual unveil update upgrade
uphold upon upper upset urban urge usage use used useful
useless usual utility vacant vacuum vague valid valley valve van
vanish vapor various vast vault vehicle velvet vendor venture venue
verb verify version very vessel veteran viable vibrant vicious victory
video view village vintage violin virtual virus visa visit visual
vital vivid vocal voice void volcano volume vote voyage wage
wagon wait walk wall walnut want warfare warm warrior wash
wasp waste water wave way wealth weapon wear weasel weather
web wedding weekend weird welcome west wet whale what wheat
wheel when where whip whisper wide width wife wild will
win window wine wing wink winner winter wire wisdom wise
wish witness wolf woman wonder wood wool word work world
worry worth wrap wreck wrestle wrist write wrong yard year
yellow you young youth zebra zero zone zoo""".split()


def _sha256(data):
    """SHA256 hash."""
    return hashlib.sha256(data).digest()


def _pbkdf2_hmac_sha512(password, salt, iterations=2048):
    """PBKDF2 with HMAC-SHA512."""
    return hashlib.pbkdf2_hmac('sha512', password, salt, iterations)


# Valid mnemonic lengths: 12, 15, 18, 21, 24 words
# Corresponding entropy bits: 128, 160, 192, 224, 256
VALID_WORD_COUNTS = (12, 15, 18, 21, 24)
VALID_STRENGTHS = (128, 160, 192, 224, 256)


def generate_mnemonic(strength=128):
    """
    Generate a BIP39 mnemonic phrase.
    
    Args:
        strength: 128 (12 words), 160 (15 words), 192 (18 words),
                  224 (21 words), or 256 (24 words)
    
    Returns:
        Space-separated mnemonic phrase
    """
    if strength not in VALID_STRENGTHS:
        raise ValueError(f"Strength must be one of {VALID_STRENGTHS}")
    
    # Generate random entropy
    entropy = secrets.token_bytes(strength // 8)
    
    # Calculate checksum
    h = _sha256(entropy)
    checksum_bits = strength // 32
    
    # Convert entropy to bits
    entropy_bits = bin(int.from_bytes(entropy, 'big'))[2:].zfill(strength)
    checksum = bin(h[0])[2:].zfill(8)[:checksum_bits]
    
    # Combine entropy and checksum
    all_bits = entropy_bits + checksum
    
    # Split into 11-bit chunks and map to words
    words = []
    for i in range(0, len(all_bits), 11):
        index = int(all_bits[i:i+11], 2)
        words.append(WORDLIST[index])
    
    return ' '.join(words)


def validate_mnemonic(mnemonic, strict_checksum=True):
    """
    Validate a BIP39 mnemonic phrase.
    
    Args:
        mnemonic: Space-separated mnemonic phrase
        strict_checksum: If True (default), verify checksum bits.
                        If False, only check word count and wordlist membership.
                        Use strict_checksum=True for production wallets.
    
    Returns:
        True if valid, False otherwise
    """
    words = mnemonic.lower().strip().split()
    
    if len(words) not in VALID_WORD_COUNTS:
        return False
    
    # Check all words are in wordlist
    for word in words:
        if word not in WORDLIST:
            return False
    
    # Skip checksum validation for compatibility with various wallets
    if not strict_checksum:
        return True
    
    # Convert words to bits
    bits = ''
    for word in words:
        index = WORDLIST.index(word)
        bits += bin(index)[2:].zfill(11)
    
    # Split into entropy and checksum
    checksum_len = len(words) // 3
    entropy_bits = bits[:-checksum_len]
    checksum_bits = bits[-checksum_len:]
    
    # Verify checksum
    entropy_bytes = int(entropy_bits, 2).to_bytes(len(entropy_bits) // 8, 'big')
    h = _sha256(entropy_bytes)
    expected_checksum = bin(h[0])[2:].zfill(8)[:checksum_len]
    
    return checksum_bits == expected_checksum


def mnemonic_to_seed(mnemonic, passphrase=""):
    """
    Convert mnemonic to BIP39 seed.
    
    Args:
        mnemonic: Space-separated mnemonic phrase
        passphrase: Optional passphrase (default empty)
    
    Returns:
        64-byte seed
    """
    mnemonic_bytes = mnemonic.encode('utf-8')
    salt = ('mnemonic' + passphrase).encode('utf-8')
    return _pbkdf2_hmac_sha512(mnemonic_bytes, salt, 2048)


def seed_to_master_key(seed):
    """
    Derive master private key from seed using BIP32.
    
    Args:
        seed: 64-byte BIP39 seed
    
    Returns:
        Tuple of (master_private_key, chain_code)
    """
    I = hmac.new(b"Bitcoin seed", seed, hashlib.sha512).digest()
    return I[:32], I[32:]


# secp256k1 curve order
SECP256K1_N = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141

# BIP44 constants
BIP44_PURPOSE = 44
BIP44_COIN_TYPE = 512  # Radiant SLIP-0044 registered coin type


def _get_public_key_bytes(private_key_bytes):
    """Get compressed public key bytes from private key."""
    k = int.from_bytes(private_key_bytes, 'big')
    point = _point_multiply(k)
    prefix = b'\x02' if point[1] % 2 == 0 else b'\x03'
    return prefix + point[0].to_bytes(32, 'big')


def _derive_child_key(parent_key, parent_chain_code, index, hardened=False):
    """
    Derive a child key from parent key using BIP32.
    
    Args:
        parent_key: 32-byte parent private key
        parent_chain_code: 32-byte parent chain code
        index: Child index (0-2^31-1 for normal, 0-2^31-1 for hardened)
        hardened: If True, use hardened derivation (index += 2^31)
    
    Returns:
        Tuple of (child_private_key, child_chain_code)
    """
    if hardened:
        # Hardened child: HMAC-SHA512(Key = parent_chain_code, Data = 0x00 || parent_key || index)
        index = index + 0x80000000
        data = b'\x00' + parent_key + struct.pack('>I', index)
    else:
        # Normal child: HMAC-SHA512(Key = parent_chain_code, Data = public_key || index)
        public_key = _get_public_key_bytes(parent_key)
        data = public_key + struct.pack('>I', index)
    
    I = hmac.new(parent_chain_code, data, hashlib.sha512).digest()
    IL, IR = I[:32], I[32:]
    
    # child_key = (IL + parent_key) mod n
    il_int = int.from_bytes(IL, 'big')
    parent_int = int.from_bytes(parent_key, 'big')
    child_int = (il_int + parent_int) % SECP256K1_N
    
    if il_int >= SECP256K1_N or child_int == 0:
        # Invalid key, try next index (extremely rare)
        raise ValueError("Invalid child key derived, try next index")
    
    child_key = child_int.to_bytes(32, 'big')
    return child_key, IR


def derive_path(seed, path):
    """
    Derive a key from seed using a BIP32 derivation path.
    
    Args:
        seed: 64-byte BIP39 seed
        path: Derivation path string (e.g., "m/44'/0'/0'/0/0")
    
    Returns:
        Tuple of (private_key, chain_code)
    """
    # Parse path
    if not path.startswith('m'):
        raise ValueError("Path must start with 'm'")
    
    master_key, chain_code = seed_to_master_key(seed)
    key, cc = master_key, chain_code
    
    # Skip 'm' and split by '/'
    components = path.split('/')[1:]
    
    for component in components:
        if not component:
            continue
        
        hardened = component.endswith("'") or component.endswith("h")
        if hardened:
            component = component[:-1]
        
        try:
            index = int(component)
        except ValueError:
            raise ValueError(f"Invalid path component: {component}")
        
        key, cc = _derive_child_key(key, cc, index, hardened)
    
    return key, cc


def derive_bip44_key(seed, account=0, change=0, address_index=0,
                     coin_type=BIP44_COIN_TYPE):
    """
    Derive a key using BIP44 path: m/44'/coin_type'/account'/change/address_index

    Args:
        seed: 64-byte BIP39 seed
        account: Account index (default 0)
        change: 0 for external (receiving), 1 for internal (change)
        address_index: Address index (default 0)
        coin_type: SLIP-0044 coin type. Defaults to Radiant's registered slot
                   512 (BIP44_COIN_TYPE). Pass coin_type=0 ONLY for the opt-in
                   legacy Bitcoin-slot path. The previous default of 0 silently
                   derived Bitcoin's slot and is corrected here; every in-tree
                   caller already passes 512 explicitly, so behavior is unchanged.

    Returns:
        Tuple of (private_key, chain_code)
    """
    path = f"m/44'/{coin_type}'/{account}'/{change}/{address_index}"
    return derive_path(seed, path)


def _point_multiply(k):
    """
    Multiply generator point by scalar k to get public key.
    Using secp256k1 curve parameters.
    """
    # secp256k1 parameters
    p = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2F
    n = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141
    Gx = 0x79BE667EF9DCBBAC55A06295CE870B07029BFCDB2DCE28D959F2815B16F81798
    Gy = 0x483ADA7726A3C4655DA4FBFC0E1108A8FD17B448A68554199C47D08FFB10D4B8
    
    def inverse_mod(a, m):
        if a < 0:
            a = a % m
        g, x, _ = extended_gcd(a, m)
        if g != 1:
            raise Exception('Modular inverse does not exist')
        return x % m
    
    def extended_gcd(a, b):
        if a == 0:
            return b, 0, 1
        gcd, x1, y1 = extended_gcd(b % a, a)
        x = y1 - (b // a) * x1
        y = x1
        return gcd, x, y
    
    def point_add(P, Q):
        if P is None:
            return Q
        if Q is None:
            return P
        if P[0] == Q[0] and P[1] != Q[1]:
            return None
        if P == Q:
            lam = (3 * P[0] * P[0]) * inverse_mod(2 * P[1], p) % p
        else:
            lam = (Q[1] - P[1]) * inverse_mod(Q[0] - P[0], p) % p
        x = (lam * lam - P[0] - Q[0]) % p
        y = (lam * (P[0] - x) - P[1]) % p
        return (x, y)
    
    G = (Gx, Gy)
    result = None
    addend = G
    
    while k:
        if k & 1:
            result = point_add(result, addend)
        addend = point_add(addend, addend)
        k >>= 1
    
    return result


def private_key_to_wif(private_key_bytes, compressed=True, testnet=False):
    """
    Convert private key bytes to WIF format.
    
    Args:
        private_key_bytes: 32-byte private key
        compressed: Use compressed public key (default True)
        testnet: Use testnet prefix (default False)
    
    Returns:
        WIF-encoded private key string
    """
    prefix = b'\xef' if testnet else b'\x80'
    suffix = b'\x01' if compressed else b''
    
    extended = prefix + private_key_bytes + suffix
    checksum = _sha256(_sha256(extended))[:4]
    
    return _base58_encode(extended + checksum)


def wif_to_private_key(wif):
    """
    Decode WIF format to private key bytes.
    
    Args:
        wif: WIF-encoded private key string
    
    Returns:
        Tuple of (private_key_bytes, compressed, testnet)
    """
    decoded = _base58_decode(wif)
    
    # Verify checksum
    checksum = decoded[-4:]
    data = decoded[:-4]
    if _sha256(_sha256(data))[:4] != checksum:
        raise ValueError("Invalid WIF checksum")
    
    prefix = data[0:1]
    testnet = prefix == b'\xef'
    
    if len(data) == 34:  # Compressed
        return data[1:33], True, testnet
    else:  # Uncompressed
        return data[1:33], False, testnet


def _base58_encode(data):
    """Base58 encode bytes."""
    alphabet = '123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz'
    
    # Count leading zeros
    leading_zeros = 0
    for byte in data:
        if byte == 0:
            leading_zeros += 1
        else:
            break
    
    # Convert to integer
    num = int.from_bytes(data, 'big')
    
    # Encode
    result = ''
    while num:
        num, remainder = divmod(num, 58)
        result = alphabet[remainder] + result
    
    return '1' * leading_zeros + result


def _base58_decode(s):
    """Base58 decode string to bytes."""
    alphabet = '123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz'
    
    # Count leading ones (zeros in output)
    leading_ones = 0
    for char in s:
        if char == '1':
            leading_ones += 1
        else:
            break
    
    # Convert to integer
    num = 0
    for char in s:
        num = num * 58 + alphabet.index(char)
    
    # Convert to bytes
    result = []
    while num:
        result.append(num & 0xff)
        num >>= 8
    
    return b'\x00' * leading_ones + bytes(reversed(result))


def mnemonic_to_wif(mnemonic, passphrase="", testnet=False, use_bip44=True, address_index=0):
    """
    Convert mnemonic directly to WIF private key using BIP44 derivation.

    Args:
        mnemonic: Space-separated mnemonic phrase
        passphrase: Optional passphrase
        testnet: Use testnet prefix
        use_bip44: If True (default), use BIP44 derivation path
                   m/44'/512'/0'/0/<address_index> (Radiant coin type)
                   If False, use master key directly (legacy behavior)
        address_index: BIP44 address index (default 0)

    Returns:
        WIF-encoded private key
    """
    if not validate_mnemonic(mnemonic):
        raise ValueError("Invalid mnemonic phrase")

    seed = mnemonic_to_seed(mnemonic, passphrase)

    if use_bip44:
        # Use BIP44 derivation: m/44'/512'/0'/0/<address_index> (Radiant coin type)
        derived_key, _ = derive_bip44_key(
            seed, account=0, change=0, address_index=address_index,
            coin_type=BIP44_COIN_TYPE)
        return private_key_to_wif(derived_key, compressed=True, testnet=testnet)
    else:
        # Legacy: use master key directly
        master_key, _ = seed_to_master_key(seed)
        return private_key_to_wif(master_key, compressed=True, testnet=testnet)


def private_key_to_address(private_key_bytes, compressed=True, testnet=False):
    """
    Derive address from private key.
    
    Args:
        private_key_bytes: 32-byte private key
        compressed: Use compressed public key
        testnet: Use testnet prefix
    
    Returns:
        Base58Check encoded address
    """
    # Get public key
    k = int.from_bytes(private_key_bytes, 'big')
    point = _point_multiply(k)
    
    if compressed:
        prefix = b'\x02' if point[1] % 2 == 0 else b'\x03'
        public_key = prefix + point[0].to_bytes(32, 'big')
    else:
        public_key = b'\x04' + point[0].to_bytes(32, 'big') + point[1].to_bytes(32, 'big')
    
    # Hash160
    sha = hashlib.sha256(public_key).digest()
    ripemd = hashlib.new('ripemd160', sha).digest()
    
    # Add version byte and checksum
    version = b'\x6f' if testnet else b'\x00'  # Note: Radiant may use different version
    extended = version + ripemd
    checksum = _sha256(_sha256(extended))[:4]
    
    return _base58_encode(extended + checksum)


if __name__ == "__main__":
    # Test with known test vector
    test_mnemonic = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about"
    print(f"Test mnemonic: {test_mnemonic}")
    print(f"Valid: {validate_mnemonic(test_mnemonic)}")
    
    # Test BIP44 derivation (m/44'/512'/0'/0/0)
    wif = mnemonic_to_wif(test_mnemonic)
    print(f"WIF (m/44'/512'/0'/0/0): {wif}")

    # Generate new mnemonic
    mnemonic = generate_mnemonic(128)
    print(f"\nGenerated mnemonic: {mnemonic}")
    print(f"Valid: {validate_mnemonic(mnemonic)}")
    wif = mnemonic_to_wif(mnemonic)
    print(f"WIF (m/44'/512'/0'/0/0): {wif}")

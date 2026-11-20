#!/usr/bin/env python3
"""Build a project-local public LibOrbisPkg/PkgTool with the macOS RSA fix."""
import hashlib
import io
import json
import os
import re
import subprocess
import tarfile
from pathlib import Path
from fetch_dependencies import get

ROOT = Path(__file__).resolve().parents[1]


def main():
    destination = ROOT / 'tools/pkgtool'
    if (destination / 'PkgTool.Core.dll').exists() and (ROOT / 'tools/pkgtool-manifest.json').exists():
        print('Project-local PkgTool already prepared.')
        return
    repo = 'maxton/LibOrbisPkg'
    commit = '643477263b2644e0803e0f58b8726ea4e3f3b7d4'
    archive = get(f'https://api.github.com/repos/{repo}/tarball/{commit}')
    source = ROOT / 'tools/pkgtool-source'
    source.mkdir(exist_ok=True)
    with tarfile.open(fileobj=io.BytesIO(archive), mode='r:gz') as tar:
        for member in tar.getmembers():
            pieces = Path(member.name).parts[1:]
            if not pieces or member.issym() or member.islnk():
                continue
            member.name = str(Path(*pieces))
            if '..' in pieces or member.name.startswith('/'):
                raise RuntimeError('Unsafe public source archive entry')
            tar.extract(member, source, filter='data')
    crypto = source / 'LibOrbisPkg/Util/Crypto.cs'
    text = crypto.read_text()
    start = text.index('public static byte[] RSA2048SignSha256(')
    body = text.index('{', start)
    depth = 1; end = body + 1
    while depth:
        if text[end] == '{': depth += 1
        if text[end] == '}': depth -= 1
        end += 1
    replacement = '''{
      // Project portability fix: standard PKCS#1 v1.5 avoids legacy AppleCrypto.
      if (sha256Hash.Length != 32)
        throw new ArgumentException("Expected SHA-256 digest", nameof(sha256Hash));
      byte[] prefix = Convert.FromHexString("3031300D060960864801650304020105000420");
      byte[] encoded = new byte[256];
      int separator = encoded.Length - prefix.Length - sha256Hash.Length - 1;
      encoded[1] = 1;
      Array.Fill(encoded, (byte)0xFF, 2, separator - 2);
      Buffer.BlockCopy(prefix, 0, encoded, separator + 1, prefix.Length);
      Buffer.BlockCopy(sha256Hash, 0, encoded, separator + 1 + prefix.Length, sha256Hash.Length);
      var message = new System.Numerics.BigInteger(encoded, isUnsigned: true, isBigEndian: true);
      var exponent = new System.Numerics.BigInteger(keyset.PrivateExponent, isUnsigned: true, isBigEndian: true);
      var modulus = new System.Numerics.BigInteger(keyset.Modulus, isUnsigned: true, isBigEndian: true);
      byte[] signature = System.Numerics.BigInteger.ModPow(message, exponent, modulus).ToByteArray(isUnsigned: true, isBigEndian: true);
      if (signature.Length > 256) throw new CryptographicException("RSA size mismatch");
      byte[] padded = new byte[256];
      Buffer.BlockCopy(signature, 0, padded, padded.Length - signature.Length, signature.Length);
      return padded;
    }'''
    original = source / 'LibOrbisPkg/Util/Crypto.cs.upstream'
    original.write_text(text)
    crypto.write_text(text[:body] + replacement + text[end:])
    for project in source.rglob('*.Core.csproj'):
        content = project.read_text()
        content = re.sub(r'<TargetFramework>[^<]+</TargetFramework>', '<TargetFramework>net8.0</TargetFramework>', content)
        project.write_text(content)
    project = source / 'PkgTool/PkgTool.Core.csproj'
    if not project.exists():
        project = next(source.rglob('PkgTool.Core.csproj'))
    dotnet = os.environ.get('DOTNET', '/Users/youssouf/.dotnet/dotnet')
    environment = dict(os.environ)
    environment.update(DOTNET_CLI_HOME=str(ROOT / 'build/dotnet-home'), NUGET_PACKAGES=str(ROOT / 'build/nuget'),
                       DOTNET_SKIP_FIRST_TIME_EXPERIENCE='1', DOTNET_CLI_TELEMETRY_OPTOUT='1', DOTNET_GENERATE_ASPNET_CERTIFICATE='false')
    destination.mkdir(exist_ok=True)
    subprocess.run([dotnet, 'build', str(project), '-c', 'Release', '-o', str(destination)], env=environment, check=True)
    license_file = source / 'LICENSE'
    if not license_file.exists():
        license_file = next(p for p in source.glob('*') if 'license' in p.name.lower())
    (ROOT / 'licenses/LibOrbisPkg-LICENSE.txt').write_bytes(license_file.read_bytes())
    files = {}
    for path in sorted(destination.glob('*')):
        if path.is_file():
            files[path.name] = {'bytes': path.stat().st_size, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
    report = {'repository': 'https://github.com/' + repo, 'commit': commit, 'source_archive_sha256': hashlib.sha256(archive).hexdigest(),
              'modified_source': 'tools/pkgtool-source', 'changes': ['Target .NET 8', 'PKCS#1 v1.5 RSA using BigInteger instead of legacy AppleCrypto'],
              'license_file': 'licenses/LibOrbisPkg-LICENSE.txt', 'files': files}
    (ROOT / 'tools/pkgtool-manifest.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()

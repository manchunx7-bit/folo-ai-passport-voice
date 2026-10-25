"""Runtime checks shared by the macOS client and setup doctor."""
import platform
import subprocess
import sys


SUPPORTED_ARCHITECTURES = ('arm64', 'x86_64')


def running_under_rosetta(system=None):
    """Return whether macOS is translating this process through Rosetta."""
    if (system or sys.platform) not in ('darwin', 'Darwin'):
        return False
    try:
        result = subprocess.run(
            ['sysctl', '-in', 'sysctl.proc_translated'],
            capture_output=True, text=True, check=False,
        )
    except OSError:
        return False
    return result.returncode == 0 and result.stdout.strip() == '1'


def runtime_supported(system=None, architecture=None, version_info=None):
    system = system or sys.platform
    architecture = architecture or platform.machine()
    version_info = version_info or sys.version_info
    return (system in ('darwin', 'Darwin')
            and architecture in SUPPORTED_ARCHITECTURES
            and not running_under_rosetta(system)
            and version_info >= (3, 11))


def runtime_error(system=None, architecture=None, version_info=None):
    system = system or sys.platform
    architecture = architecture or platform.machine()
    version_info = version_info or sys.version_info
    if system not in ('darwin', 'Darwin'):
        return '请在 macOS 上运行'
    if running_under_rosetta(system):
        return '不支持通过 Rosetta 运行，请使用原生 arm64 或 Intel x86_64 Python'
    if architecture not in SUPPORTED_ARCHITECTURES:
        return '请使用原生 arm64 或 x86_64 Python'
    if version_info < (3, 11):
        return '请安装 Python 3.11 或更新版本'
    return ''

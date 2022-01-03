from zope.interface.declarations import implementer
from twisted.cred.checkers import ICredentialsChecker
from twisted.cred import credentials, error
from twisted.internet import defer
from twisted.python import failure

from exchangelib import Account

import logging
log = logging.getLogger(__name__)


class ExchangeAccount:
    def __init__(self, account: Account, username: str):
        self.username = username
        self.ews_account = account
        log.info(f"opened account for {username}")

    def authenticate(username: str, password: str):
        return None
        account.root.refresh()
        return ExchangeAccount(account, username)


@implementer(ICredentialsChecker)
class ExchangeCredentialsChecker:
    credentialInterfaces = {
        credentials.IUsernamePassword
    }

    def requestAvatarId(self, creds: credentials.IUsernamePassword):
        account = ExchangeAccount.authenticate(creds.username, creds.password)
        if not account:
            log.warning(f"cannot authenticate: username={creds.username} password=...")
            return failure.Failure(error.UnauthorizedLogin())
        return ExchangeAccount(account, creds.username)

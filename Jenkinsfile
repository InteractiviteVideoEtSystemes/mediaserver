def PROJET = 'mcumediaserver'
def PROJET2 = ''

import hudson.model.*
import jenkins.model.*
import hudson.tasks.test.AbstractTestResultAction

pipeline {
  agent none
  parameters {
    booleanParam(name: 'SIGNRPM', defaultValue: false, description: 'Signer le RPM ?')
    string(name: 'PARAPHRASE', defaultValue: '' , description: 'Paraphrase de la cle de signature')
  }
  stages {
    stage('Verification paraphrase') {
       agent { label 'centos6' }
       when {
          expression { params.SIGNRPM }
       }
       steps {
          sh """
          svn export https://svn.ives.fr/svn-libs-dev/gnupg
          echo "${params.PARAPHRASE}" |  gpg -q --local-user IVeSkey --batch --homedir ./gnupg/ --passphrase-fd 0 --sign > /dev/null
          """
       }
    }
    stage('CompilEtInstalle') {
      matrix {
        agent { label "${NODELABEL}" }
        axes {
           axis {
               name 'NODELABEL'
               values 'centos6', 'centos7'
           }
	}
	stages {
	   stage('Generation RPM') { 

               steps {
	           sh """
		   ./install.ksh prereq
                   ./install.ksh rpm nosign
	           """
              }
           }
           stage('Signer le RPM') {
              when {
                 expression { params.SIGNRPM }
              }
              steps {
		sh """
		svn export https://svn.ives.fr/svn-libs-dev/gnupg
		echo \"${params.PASSPHRASE}\" | rpm --resign ${PROJET}*.rpm"
          	rm -rf ./gnupg
		"""
              }
           }
           stage('Inst. le RPM') {
               when {
                   buildingTag()
               }
               steps {
	           sh """
                   sudo yum remove -y ${PROJET} 
                   sudo yum localinstall -y ${PROJET}*.rpm
	           """
               }
           }
 
           stage('archive') {
               when {
                   buildingTag()
               }
               steps {
                   archiveArtifacts(artifacts: '${PROJET}*.rpm,vrn.html', onlyIfSuccessful: true)
               }
           }
        } // stages 
        post {
	   failure {
              script {
	        notifFail("Failed to build RPM on ${NODELABEL}")
              }
           }
        }
      } // matrix
    } // stage('BuildAndInstall')
  } // stages
  post {
    success {
        script{
           notifSuccess()
        }
    }
  }
}

void notifFail(e)
{
  office365ConnectorSend(message: "💔:  *BUILD FAIL asteriskv $BRANCH_NAME* : \n\n Lien du Build : <RUN_DISPLAY_URL|JENKINS-#$BUILD_NUMBER> \n \n Cordialement, Jenkins", status: 'Success', webhookUrl: 'https://outlook.office.com/webhook/a8d2a9bb-d91a-48b9-8774-a1907c4bce10@dda7df9a-8948-410e-8cd6-c830a3370b09/JenkinsCI/1c7e9437aaad42a8a2407afabdbfc096/37c3465b-9ab9-4181-8220-d38b79c27fe3', color:'00ff00')

}

void notifSuccess()
{
  office365ConnectorSend(message: "✅:  *BUILD SUCCESS asteriskv $BRANCH_NAME* : \n\n Lien du Build  <$RUN_DISPLAY_URL|JENKINS-#$BUILD_NUMBER> \n \n Cordialement, Jenkins", status: 'Success', webhookUrl: 'https://outlook.office.com/webhook/a8d2a9bb-d91a-48b9-8774-a1907c4bce10@dda7df9a-8948-410e-8cd6-c830a3370b09/JenkinsCI/1c7e9437aaad42a8a2407afabdbfc096/37c3465b-9ab9-4181-8220-d38b79c27fe3', color:'00ff00')
}

void notifSuccessInstalled()
{
  office365ConnectorSend(message: "✅:  *BUILD SUCCESS asteriskv $BRANCH_NAME* : \n\n Lien du Build : $RUN_DISPLAY_URL|JENKINS-#$BUILD_NUMBER> \n \n Cordialement, Jenkins", status: 'Success', webhookUrl: 'https://outlook.office.com/webhook/a8d2a9bb-d91a-48b9-8774-a1907c4bce10@dda7df9a-8948-410e-8cd6-c830a3370b09/JenkinsCI/1c7e9437aaad42a8a2407afabdbfc096/37c3465b-9ab9-4181-8220-d38b79c27fe3', color:'00ff00')
}

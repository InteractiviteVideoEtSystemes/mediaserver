def VERSION = ''
def PROJET = ''
def DESTDIR = ''
def SLACKALREADYSENT = false
import hudson.model.*
import jenkins.model.*
import hudson.tasks.test.AbstractTestResultAction
import groovy.json.JsonSlurper

def getJobStatus(String jobName){
	def job_data = Jenkins.instance.getItemByFullName(jobName)
    if (job_data.getLastBuild())
	{
		return job_data.getLastBuild().getResult().toString()
	}
}

pipeline {
  agent any
  stages {
    stage('Build RPM') {
      steps {

        sh "./install.ksh export"
        script{
          def props = readJSON file:'build.properties';
          VERSION = props['VERSION']
          PROJET = props['PROJET']
          DESTDIR = props['DESTDIR']

				  jobStatus = getJobStatus("/pltf/ffmpeg")
					echo "getting latest build from ffmpeg:"
					echo jobStatus
				  if(jobStatus != "SUCCESS" )
					{
						build "/pltf/ffmpeg"
				  }

				  jobStatus = getJobStatus("/pltf/libbfcp")
					echo "getting latest build from libbfcp:"
					echo jobStatus
				  if(jobStatus != "SUCCESS" )
					{
					  build "/pltf/libbfcp"
				  }

				  jobStatus = getJobStatus("/pltf/mp4v2")
					echo "getting latest build from mp4v2:"
					echo jobStatus
				  if(jobStatus != "SUCCESS" )
				  {
					 build "/pltf/mp4v2"
				  }
				}
        sh "svn export https://svn.ives.fr/svn-libs-dev/gnupg"
        sh """
        mkdir -p rpmbuild
        mkdir -p rpmbuild/SOURCES
        mkdir -p rpmbuild/SPECS
        mkdir -p rpmbuild/BUILD
        mkdir -p rpmbuild/SRPMS
        mkdir -p rpmbuild/TMP@
        mkdir -p rpmbuild/RPMS
        mkdir -p rpmbuild/RPMS/noarch
        mkdir -p rpmbuild/RPMS/i386
		mkdir -p rpmbuild/RPMS/x86_64
        mkdir -p rpmbuild/RPMS/i686
        mkdir -p rpmbuild/RPMS/i586

        cp ${PROJET}.spec ./rpmbuild/SPECS/${PROJET}.spec

        rm ~/.rpmmacros
        touch ~/.rpmmacros
        echo "%_topdir" $WORKSPACE"/rpmbuild" >> ~/.rpmmacros
        echo "%_tmppath %{_topdir}/TMP" >> ~/.rpmmacros
        echo "%name $PROJET" >> ~/.rpmmacros
        echo "%version $VERSION" >> ~/.rpmmacros
        echo "%_signature gpg" >> ~/.rpmmacros
        echo "%_gpg_name IVeSkey" >> ~/.rpmmacros
        echo "%_gpg_path" $WORKSPACE"/gnupg" >> ~/.rpmmacros
        echo "%vendor IVeS" >> ~/.rpmmacros
        #Import de la clef gpg IVeS
        """

        sh "rpmbuild -bb rpmbuild/SPECS/${PROJET}.spec"
		sh "mv $WORKSPACE/rpmbuild/RPMS/x86_64/*.rpm $WORKSPACE/."

      }
    }

  stage('Sign') {
    when {
      buildingTag()
    }
    parallel{
      stage('Signature'){
        input{
          message "Signature du RPM"
          ok "Signer le RPM"
          parameters {
            password(defaultValue: '', description: 'Merci d\'entrer la Passphrase pour la signature du RPM', name: 'PASSPHRASE')
          }
        }
        steps {
          sh "echo ${PASSPHRASE} | rpm --resign ${PROJET}*.rpm"
        }
      }
    }
  }
  stage('E-mail and archive') {
    when {
      buildingTag()
    }
    steps {
      slackSuccessInstalled()
      emailext(attachmentsPattern: 'vrn.html', body: 'Livraison de <strong>mediaserver $BRANCH_NAME</strong> : <br/><br/>       Lien GIT : <a href="https://github.com/InteractiviteVideoEtSystemes/mediaserver/$BRANCH_NAME">http://git.ives.fr/plateformes/kamailioconf/tree/$BRANCH_NAME</a><br/><br/> Lien du Build : <a href="$RUN_DISPLAY_URL">$RUN_DISPLAY_URL</a>       <br/><br/>       Cordialement,<br/>       Jenkins', mimeType: 'text/html', replyTo: 'devplateforme@ives.fr', subject: '[mediaserver] Livraison $BRANCH_NAME', recipientProviders: [requestor()], to: "jenkins@ives.fr")
      archiveArtifacts(artifacts: '*.rpm,vrn.html', onlyIfSuccessful: true)
      script{
        SLACKALREADYSENT=true
      }
    }
  }
}
  post {
     failure {
         script{
                slackFail("An error occured")
          }
    }
    success {
        script{
            if(!SLACKALREADYSENT){
                slackSuccess()
            }
        }
    }
  }
}

void slackFail(e)
{
  slackSend(message: ":x: *BUILD FAIL mediaserver $BRANCH_NAME* : ```${e}``` \n\n Lien du Build : <$RUN_DISPLAY_URL|JENKINS-#$BUILD_NUMBER> \n Cordialement, Jenkins", baseUrl: 'https://ives-workspace.slack.com/services/hooks/jenkins-ci/', channel: 'jenkins', color: 'danger', teamDomain: 'ives-workspace.slack.com', token: 'gcwQATpI47iPCfTcPCcdR4ZR')
}

void slackSuccess()
{
  slackSend(message: ":white_check_mark:  *BUILD SUCCESS mediaserver $BRANCH_NAME* : \n\n Lien du Build : <$RUN_DISPLAY_URL|JENKINS-#$BUILD_NUMBER> \n \n Cordialement, Jenkins", baseUrl: 'https://ives-workspace.slack.com/services/hooks/jenkins-ci/', channel: 'jenkins', color: 'good', teamDomain: 'ives-workspace.slack.com', token: 'gcwQATpI47iPCfTcPCcdR4ZR')
}

void slackSuccessInstalled()
{
  slackSend(message: ":white_check_mark: *BUILD SUCCESS mediaserver $BRANCH_NAME* : \n\n Lien du Build : <$RUN_DISPLAY_URL|JENKINS-#$BUILD_NUMBER> \n \n Cordialement, Jenkins", baseUrl: 'https://ives-workspace.slack.com/services/hooks/jenkins-ci/', channel: 'jenkins', color: 'good', teamDomain: 'ives-workspace.slack.com', token: 'gcwQATpI47iPCfTcPCcdR4ZR')
}
